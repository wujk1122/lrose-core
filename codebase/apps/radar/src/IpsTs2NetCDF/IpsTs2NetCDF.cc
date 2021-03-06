// *=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=* 
// ** Copyright UCAR (c) 1990 - 2016                                         
// ** University Corporation for Atmospheric Research (UCAR)                 
// ** National Center for Atmospheric Research (NCAR)                        
// ** Boulder, Colorado, USA                                                 
// ** BSD licence applies - redistribution and use in source and binary      
// ** forms, with or without modification, are permitted provided that       
// ** the following conditions are met:                                      
// ** 1) If the software is modified to produce derivative works,            
// ** such modified software should be clearly marked, so as not             
// ** to confuse it with the version available from UCAR.                    
// ** 2) Redistributions of source code must retain the above copyright      
// ** notice, this list of conditions and the following disclaimer.          
// ** 3) Redistributions in binary form must reproduce the above copyright   
// ** notice, this list of conditions and the following disclaimer in the    
// ** documentation and/or other materials provided with the distribution.   
// ** 4) Neither the name of UCAR nor the names of its contributors,         
// ** if any, may be used to endorse or promote products derived from        
// ** this software without specific prior written permission.               
// ** DISCLAIMER: THIS SOFTWARE IS PROVIDED "AS IS" AND WITHOUT ANY EXPRESS  
// ** OR IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED      
// ** WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.    
// *=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=* 
///////////////////////////////////////////////////////////////
// IpsIpsTs2NetCDF.cc
//
// Mike Dixon, RAP, NCAR, P.O.Box 3000, Boulder, CO, 80307-3000, USA
//
// Dec 2020
//
///////////////////////////////////////////////////////////////
//
// Support for Independent Pulse Sampling.
//
// IpsTs2NetCDF reads time-series data and saves it out as netCDF
//
////////////////////////////////////////////////////////////////

#include "IpsTs2NetCDF.hh"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <cerrno>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctime>
#include <toolsa/Path.hh>
#include <toolsa/TaArray.hh>
#include <toolsa/DateTime.hh>
#include <toolsa/uusleep.h>
#include <toolsa/toolsa_macros.h>
#include <toolsa/pmu.h>
#include <toolsa/file_io.h>
#include <radar/RadarComplex.hh>

using namespace std;

// Constructor

IpsTs2NetCDF::IpsTs2NetCDF(int argc, char **argv)
  
{

  isOK = true;
  _pulseReader = NULL;
  _nPulsesRead = 0;
  
  _startRangeM = 0.0;
  _gateSpacingM = 1.0;

  // set programe name
  
  _progName = "IpsTs2NetCDF";

  // get command line args

  if (_args.parse(argc, argv, _progName)) {
    cerr << "ERROR: " << _progName << endl;
    cerr << "Problem with command line args" << endl;
    isOK = false;
    return;
  }

  // get TDRP params
  
  _paramsPath = (char *) "unknown";
  if (_params.loadFromArgs(argc, argv, _args.override.list,
			   &_paramsPath)) {
    cerr << "ERROR: " << _progName << endl;
    cerr << "Problem with TDRP parameters" << endl;
    isOK = false;
    return;
  }

  if (_params.debug >= Params::DEBUG_EXTRA) {
    cerr << "Running " << _progName << " - extra verbose debug mode" << endl;
  } else if (_params.debug >= Params::DEBUG_VERBOSE) {
    cerr << "Running " << _progName << " - verbose debug mode" << endl;
  } else if (_params.debug) {
    cerr << "Running " << _progName << " - debug mode" << endl;
  }

  // create the pulse reader
  
  IpsTsDebug_t ipsDebug = IpsTsDebug_t::OFF;
  if (_params.debug >= Params::DEBUG_EXTRA) {
    ipsDebug = IpsTsDebug_t::VERBOSE;
  } else if (_params.debug >= Params::DEBUG_NORM) {
    ipsDebug = IpsTsDebug_t::NORM;
  } 
    
  if (_params.input_mode == Params::TS_FMQ_INPUT) {
    _pulseReader = new IpsTsReaderFmq(_params.input_fmq_name,
				       ipsDebug,
				       !_params.seek_to_end_of_input);
  } else if (_params.input_mode == Params::TS_TCP_INPUT) {
    _pulseReader = new IpsTsReaderTcp(_params.tcp_server_host,
                                       _params.tcp_server_port,
				       ipsDebug);
  } else {
    _pulseReader = new IpsTsReaderFile(_args.inputFileList, ipsDebug);
  }

  // override cal if appropriate

  if (_params.override_radar_cal) {
    string errStr;
    if (_calOverride.readFromXmlFile
        (_params.radar_cal_xml_file_path, errStr)) {
      cerr << "ERROR - IpsTs2NetCDF::Run" << endl;
      cerr << "  Overriding radar cal" << endl;
      cerr << "  Cannot read in cal file: " 
           << _params.radar_cal_xml_file_path << endl;
      cerr << errStr << endl;
      isOK = false;
      return;
    }
  }

  // init process mapper registration
  
  if (_params.reg_with_procmap) {
    PMU_auto_init((char *) _progName.c_str(),
                  _params.instance,
                  PROCMAP_REGISTER_INTERVAL);
  }
  
}

// destructor

IpsTs2NetCDF::~IpsTs2NetCDF()

{

  _clearPulseQueue();
  
  if (_pulseReader) {
    delete _pulseReader;
  }

  PMU_auto_unregister();

}

//////////////////////////////////////////////////
// Run

int IpsTs2NetCDF::Run ()
{

  if (_params.debug) {
    cerr << "==>> reading in pulses" << endl;
  }
  
  while (true) {
    
    PMU_auto_register("Reading pulse");
    
    // read next pulse

    IpsTsPulse *pulse = _pulseReader->getNextPulse(true);

    // convert to floats
    
    if (pulse != NULL) {
      _nPulsesRead++;
      if (_params.debug) {
        if ((_nPulsesRead % 1000) == 0) {
          cerr << "El, az, npulses received: "
               << pulse->getElevation() << ", "
               << pulse->getAzimuth() << ", "
               << _nPulsesRead << endl; 
        }
      }
    }

    // check we are ready to write the data to file

    bool endOfFile = false;
    bool readyToWrite = _checkReadyToWrite(pulse, endOfFile);
    
    // add to queue if we are to use this pulse now

    if (readyToWrite) {

      if (_pulses.size() > 0) {
        
        // prepare to write
        _prepareToWrite();
        
        // write the file
        if (_writeFile()) {
          return -1;
        }

      }

      // clear the queue

      _clearPulseQueue();
      
      if (pulse == NULL) {
        return 0;
      }
      
      // add previous pulse in end of file condition
      
      if (!endOfFile) {
        // add pulse to queue after write
        _pulses.push_back(pulse);
      }

    } else {

      // add pulse to queue for later write
      
      if (pulse != NULL) {
        _pulses.push_back(pulse);
      }

    } // if (readyToWrite)

  } // while

  // should not get here
  
  return -1;
  
}

///////////////////////////////////////////
// Check if we are ready to write the file
//
// Returns 0 to continue, -1 to exit

bool IpsTs2NetCDF::_checkReadyToWrite(const IpsTsPulse *pulse,
                                       bool &endOfFile)

{

  endOfFile = false;
  
  // if null pulse, we are at end of data
  
  if (pulse == NULL) {
    return true;
  }
  
  // base decision on end-of-file?
  
  if (_params.input_mode == Params::TS_FILE_INPUT &&
      _params.output_trigger == Params::END_OF_INPUT_FILE) {
    if (_pulseReader->endOfFile()) {
      endOfFile = true;
      return true;
    }
  }

  // end of sweep or volume
  
  int pulseSweepNum = pulse->getSweepNum();
  // initialize
  if (_prevPulseSweepNum == -1) {
    _prevPulseSweepNum = pulseSweepNum;
  }
  
  if (_params.output_trigger == Params::END_OF_VOLUME) {
    // look for sweep number reset to 0
    if (pulseSweepNum == 0 && _prevPulseSweepNum != 0) {
      _prevPulseSweepNum = pulseSweepNum;
      return true;
    }
  } else {
    // look for sweep number change
    if (pulseSweepNum != _prevPulseSweepNum) {
      _prevPulseSweepNum = pulseSweepNum;
      return true;
    }
  }
  
  // do we have too many pulses in the file?
  
  if ((int) _pulses.size() > _params.max_pulses_per_file) {
    return true;
  }

  return false;

}

//////////////////////////////////////////////////
// prepare for writing the file

void IpsTs2NetCDF::_prepareToWrite()
{

  if (_params.debug >= Params::DEBUG_EXTRA) {
    _checkForMissingPulses();
  }

  // initialize
  
  _computeNGatesMax();
  _nTimes = _pulses.size();

  _pulseII.prepare(_nGatesMax * sizeof(float));
  _pulseQQ.prepare(_nGatesMax * sizeof(float));
  
  _II.free();
  _QQ.free();

  _nGatesRay.clear();
  _timeRay.clear();
  _dtimeRay.clear();
  _elRay.clear();
  _azRay.clear();
  _fixedAngleRay.clear();
  _prtRay.clear();
  _pulseWidthRay.clear();

  _pulseSeqNumRay.clear();
  _dwellSeqNumRay.clear();
  _beamNumInDwellRay.clear();
  _visitNumInBeamRay.clear();
  _scanModeRay.clear();
  _sweepNumRay.clear();
  _volNumRay.clear();
  _hvFlagRay.clear();
  _chanIsCopolRay.clear();

  // loop through the pulses
  
  for (size_t ii = 0; ii < _pulses.size(); ii++) {
    
    IpsTsPulse *pulse = _pulses[ii];
    pulse->convertToFL32();
    double el = pulse->getElevation();
    double az = pulse->getAzimuth();
    
    if (ii == 0) {
      _startTime = pulse->getTime();
      _startEl = el;
      _startAz = az;
      _startFixedAngle = pulse->getFixedAngle();
      _startRangeM = pulse->getStartRangeM();
      _gateSpacingM = pulse->getGateSpacingM();
      _startScanMode = pulse->getScanMode();
    }
    
    // set pulse properties
    
    int nGates = pulse->getNGates();
    time_t pulseTimeSecs = pulse->getTime();
    double pulseTime = pulse->getFTime();
    float prt = pulse->getPrtNext();
    
    _nGatesRay.push_back(nGates);
    _timeRay.push_back(pulseTime);
    double deltaTime = pulseTimeSecs - _startTime + pulse->getNanoSecs() * 1.0e-9;
    _dtimeRay.push_back(deltaTime);
    _elRay.push_back(el);
    _azRay.push_back(az);
    _fixedAngleRay.push_back(pulse->getFixedAngle());
    _prtRay.push_back(prt);
    _pulseWidthRay.push_back(pulse->getPulseWidthUs());

    _pulseSeqNumRay.push_back(pulse->getPulseSeqNum());
    _dwellSeqNumRay.push_back(pulse->getDwellSeqNum());

    _beamNumInDwellRay.push_back(pulse->getBeamNumInDwell());
    _visitNumInBeamRay.push_back(pulse->getVisitNumInBeam());
    _scanModeRay.push_back((int) pulse->getScanMode());
    _sweepNumRay.push_back(pulse->getSweepNum());
    _volNumRay.push_back(pulse->getVolumeNum());
    _hvFlagRay.push_back(pulse->getHvFlag());
    _chanIsCopolRay.push_back(pulse->getChanIsCopol(0));

    const fl32 *chan0 = pulse->getIq0();
    float *ivals0 = (float *) _pulseII.getPtr();
    float *qvals0 = (float *) _pulseQQ.getPtr();
    
    for (int igate = 0; igate < nGates; igate++, ivals0++, qvals0++) {
      *ivals0 = *chan0;
      chan0++;
      *qvals0 = *chan0;
      chan0++;
    } // igate
    for (int igate = nGates; igate < _nGatesMax; igate++, ivals0++, qvals0++) {
      *ivals0 = -9999.0;
      *qvals0 = -9999.0;
    } // igate
    
    _II.add(_pulseII.getPtr(), _nGatesMax * sizeof(float));
    _QQ.add(_pulseQQ.getPtr(), _nGatesMax * sizeof(float));

  }  // ii
  
}

//////////////////////////////////////////////////
// compute the max number of gates

void IpsTs2NetCDF::_computeNGatesMax()
{
  
  _nGatesMax = 0;
  for (size_t ii = 0; ii < _pulses.size(); ii++) {
    int nGates = _pulses[ii]->getNGates();
    if (nGates > _nGatesMax) {
      _nGatesMax = nGates;
    }
  }  // ii
  
}

//////////////////////////////////////////////////
// check for missing pulses

void IpsTs2NetCDF::_checkForMissingPulses()
{
  
  
  si64 prevSeqNum = _pulses[0]->getSeqNum();
  
  for (size_t ii = 1; ii < _pulses.size(); ii++) {

    IpsTsPulse *pulse = _pulses[ii];
    si64 seqNum = pulse->getSeqNum();
    double el = pulse->getElevation();
    double az = pulse->getAzimuth();
    
    if (prevSeqNum > 0 && seqNum != (prevSeqNum + 1)) {
      cerr << "Missing sequence numbers, el, az, prev, this: "
           << el << ", "
           << az << ", "
           << prevSeqNum << ", "
           << seqNum << endl;
    }

  } // ii

}

////////////////////////////////////////
// clear the pulse queue

void IpsTs2NetCDF::_clearPulseQueue()

{
  for (size_t ii = 0; ii < _pulses.size(); ii++) {
    delete _pulses[ii];
  }
  _pulses.clear();
}

////////////////////////////////////////
// write out the netDCF file
//
// Returns 0 on success, -1 on failure

int IpsTs2NetCDF::_writeFile()
  
{

  // compute output and tmp paths
  
  if (_computeOutputFilePaths()) {
    return -1;
  }

  // write out tmp file
  
  if (_writeFileTmp()) {
    cerr << "ERROR - IpsTs2NetCDF::_writeFile" << endl;
    cerr << "  Cannot write netCDF tmp file: " << _tmpPath << endl;
    return -1;
  }
  
  if (_params.debug) {
    cerr << "  Wrote tmp file: " << _tmpPath << endl;
  }

  // move the tmp file to final name
  
  if (rename(_tmpPath.c_str(), _outputPath.c_str())) {
    int errNum = errno;
    cerr << "ERROR - IpsTs2NetCDF::_writeFile" << endl;
    cerr << "  Cannot rename file: " << _tmpPath << endl;
    cerr << "             to file: " << _outputPath << endl;
    cerr << "  " << strerror(errNum) << endl;
    return -1;
  }

  if (_params.debug) {
    cerr << "  Renamed file: " << _tmpPath << endl;
    cerr << "       to file: " << _outputPath << endl;
    cerr << "  ================================================" << endl;
  }

  cerr << "  Wrote file: " << _outputPath << endl;

  return 0;

}

////////////////////////////////////////
// write out the netDCF file to tmp name
// in ips format
// Returns 0 on success, -1 on failure

int IpsTs2NetCDF::_writeFileTmp()
  
{

  ////////////////////////
  // create Nc3File object
  
  NcxxFile file;
  try {
    file.open(_tmpPath, NcxxFile::replace, NcxxFile::nc4);
  } catch (NcxxException& e) {
    cerr << "ERROR - IpsTs2NetCDF::_writeFileTmp" << endl;
    cerr << "  Cannot open tmp Ncxx file for writing: " << _tmpPath << endl;
    cerr << "  exception: " << e.what() << endl;
    return -1;
  }
  int iret = 0;

  // global attributes

  _addGlobAtt(file);
  
  //////////////////
  // add dimensions

  NcxxDim gatesDim = file.addDim("gates", _nGatesMax);
  NcxxDim timeDim = file.addDim("time", _nTimes);
  NcxxDim rowsElementsDim = file.addDim("rowselements", 1);

  /////////////////////////////////
  // add vars and their attributes
  
  if (_writeBaseTimeVars(file)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeFileTmp" << endl;
    return -1;
  }

  if (_writeRangeVar(file, gatesDim)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeFileTmp" << endl;
    return -1;
  }

  if (_writeTimeDimVars(file, timeDim)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeFileTmp" << endl;
    return -1;
  }

  if (_pulses.size() > 0) {
    if (_writeIqVars(file, timeDim, gatesDim, rowsElementsDim,
                     "I", "Q",
                     (float *) _II.getPtr(),
                     (float *) _QQ.getPtr())) {
      return -1;
    }
  }

  return iret;

}

////////////////////////////////////////
// add global attributes

void IpsTs2NetCDF::_addGlobAtt(NcxxFile &file)
  
{

  int startingSample = 0;
  int endingSample = startingSample + _nTimes - 1;
  int startGate = 0;
  int endGate = startGate + _nGatesMax - 1;
  
  char desc[1024];
  sprintf(desc,
	  "Radar time series reformatted by IpsTs2NetCDF\n"
	  "  This is the IPS format\n"
	  "  Starting Sample =%d, Ending Sample =%d, "
	  "  Start Gate= %d, End Gate = %d\n"
	  "  Azimuth = %.2f, Elevation = %.2f\n",
	  startingSample, endingSample, startGate, endGate,
	  _startAz, _startEl);

  file.addGlobAttr("Description", desc);
  file.addGlobAttr("FirstGate", startGate);
  file.addGlobAttr("LastGate", endGate);
  
  // radar info
  
  const IpsTsInfo &info = _pulses[0]->getTsInfo();
  const ips_ts_radar_info_t &radarInfo = info.getRadarInfo();
  
  file.addGlobAttr("radar_latitude_deg", radarInfo.latitude_deg);
  file.addGlobAttr("radar_longitude_deg", radarInfo.longitude_deg);
  file.addGlobAttr("radar_altitude_m", radarInfo.altitude_m);
  file.addGlobAttr("radar_platform_type", radarInfo.platform_type);
  file.addGlobAttr("radar_beamwidth_deg_h", radarInfo.beamwidth_deg_h);
  file.addGlobAttr("radar_beamwidth_deg_v", radarInfo.beamwidth_deg_v);
  file.addGlobAttr("radar_wavelength_cm", radarInfo.wavelength_cm);
  file.addGlobAttr("radar_nominal_gain_ant_db_h", 
                   radarInfo.nominal_gain_ant_db_h);
  file.addGlobAttr("radar_name", radarInfo.radar_name);
  file.addGlobAttr("radar_site_name", radarInfo.site_name);
  
  // scan info

  // const ips_ts_scan_segment_t &scanSeg = info.getScanSegment();
  
  // file.addGlobAttr("scan_scan_mode", 
  //                  ips_ts_scan_mode_to_str(scanSeg.scan_mode).c_str());
  // file.addGlobAttr("scan_volume_num", scanSeg.volume_num);
  // file.addGlobAttr("scan_sweep_num", scanSeg.sweep_num);
  // file.addGlobAttr("scan_az_start", scanSeg.az_start);
  // file.addGlobAttr("scan_el_start", scanSeg.el_start);
  // file.addGlobAttr("scan_scan_rate", scanSeg.scan_rate);
  // file.addGlobAttr("scan_left_limit", scanSeg.left_limit);
  // file.addGlobAttr("scan_right_limit", scanSeg.right_limit);
  // file.addGlobAttr("scan_up_limit", scanSeg.up_limit);
  // file.addGlobAttr("scan_down_limit", scanSeg.down_limit);
  // file.addGlobAttr("scan_step", scanSeg.step);
  // file.addGlobAttr("scan_current_fixed_angle", scanSeg.current_fixed_angle);
  // file.addGlobAttr("scan_n_sweeps", scanSeg.n_sweeps);
  // file.addGlobAttr("scan_sun_scan_sector_width_az", 
  //                  scanSeg.sun_scan_sector_width_az);
  // file.addGlobAttr("scan_sun_scan_sector_width_el", 
  //                  scanSeg.sun_scan_sector_width_el);
  // file.addGlobAttr("scan_segment_name", scanSeg.segment_name);
  // file.addGlobAttr("scan_project_name", scanSeg.project_name);

  // processing info
  
  file.addGlobAttr("proc_pulse_width_us", info.getProcPulseWidthUs());
  file.addGlobAttr("proc_start_range_m", info.getProcStartRangeM());
  file.addGlobAttr("proc_gate_spacing_m", info.getProcGateSpacingM());

  // calibration

  if (_params.override_radar_cal) {

    const DsRadarCalib &cal(_calOverride);
    file.addGlobAttr("cal_wavelength_cm", (float) cal.getWavelengthCm());
    file.addGlobAttr("cal_beamwidth_deg_h", (float) cal.getBeamWidthDegH());
    file.addGlobAttr("cal_beamwidth_deg_v", (float) cal.getBeamWidthDegV());
    file.addGlobAttr("cal_gain_ant_db_h", (float) cal.getAntGainDbH());
    file.addGlobAttr("cal_gain_ant_db_v", (float) cal.getAntGainDbV());
    file.addGlobAttr("cal_pulse_width_us", (float) cal.getPulseWidthUs());
    file.addGlobAttr("cal_xmit_power_dbm_h", (float) cal.getXmitPowerDbmH());
    file.addGlobAttr("cal_xmit_power_dbm_v", (float) cal.getXmitPowerDbmV());
    file.addGlobAttr("cal_two_way_waveguide_loss_db_h",
                     (float) cal.getTwoWayWaveguideLossDbH());
    file.addGlobAttr("cal_two_way_waveguide_loss_db_v",
                     (float) cal.getTwoWayWaveguideLossDbV());
    file.addGlobAttr("cal_two_way_radome_loss_db_h",
                     (float) cal.getTwoWayRadomeLossDbH());
    file.addGlobAttr("cal_two_way_radome_loss_db_v",
                     (float) cal.getTwoWayRadomeLossDbV());
    file.addGlobAttr("cal_receiver_mismatch_loss_db",
                     (float) cal.getReceiverMismatchLossDb());
    file.addGlobAttr("cal_radar_constant_h", (float) cal.getRadarConstH());
    file.addGlobAttr("cal_radar_constant_v", (float) cal.getRadarConstV());
    file.addGlobAttr("cal_noise_dbm_hc", (float) cal.getNoiseDbmHc());
    file.addGlobAttr("cal_noise_dbm_hx", (float) cal.getNoiseDbmHx());
    file.addGlobAttr("cal_noise_dbm_vc", (float) cal.getNoiseDbmVc());
    file.addGlobAttr("cal_noise_dbm_vx", (float) cal.getNoiseDbmVx());
    file.addGlobAttr("cal_receiver_gain_db_hc", (float) cal.getReceiverGainDbHc());
    file.addGlobAttr("cal_receiver_gain_db_hx", (float) cal.getReceiverGainDbHx());
    file.addGlobAttr("cal_receiver_gain_db_vc", (float) cal.getReceiverGainDbVc());
    file.addGlobAttr("cal_receiver_gain_db_vx", (float) cal.getReceiverGainDbVx());
    file.addGlobAttr("cal_receiver_slope_hc", (float) cal.getReceiverSlopeDbHc());
    file.addGlobAttr("cal_receiver_slope_hx", (float) cal.getReceiverSlopeDbHx());
    file.addGlobAttr("cal_receiver_slope_vc", (float) cal.getReceiverSlopeDbVc());
    file.addGlobAttr("cal_receiver_slope_vx", (float) cal.getReceiverSlopeDbVx());
    file.addGlobAttr("cal_base_dbz_1km_hc", (float) cal.getBaseDbz1kmHc());
    file.addGlobAttr("cal_base_dbz_1km_hx", (float) cal.getBaseDbz1kmHx());
    file.addGlobAttr("cal_base_dbz_1km_vc", (float) cal.getBaseDbz1kmVc());
    file.addGlobAttr("cal_base_dbz_1km_vx", (float) cal.getBaseDbz1kmVx());
    file.addGlobAttr("cal_sun_power_dbm_hc", (float) cal.getSunPowerDbmHc());
    file.addGlobAttr("cal_sun_power_dbm_hx", (float) cal.getSunPowerDbmHx());
    file.addGlobAttr("cal_sun_power_dbm_vc", (float) cal.getSunPowerDbmVc());
    file.addGlobAttr("cal_sun_power_dbm_vx", (float) cal.getSunPowerDbmVx());
    file.addGlobAttr("cal_noise_source_power_dbm_h",
                     (float) cal.getNoiseSourcePowerDbmH());
    file.addGlobAttr("cal_noise_source_power_dbm_v",
                     (float) cal.getNoiseSourcePowerDbmV());
    file.addGlobAttr("cal_power_meas_loss_db_h", (float) cal.getPowerMeasLossDbH());
    file.addGlobAttr("cal_power_meas_loss_db_v", (float) cal.getPowerMeasLossDbV());
    file.addGlobAttr("cal_coupler_forward_loss_db_h",
                     (float) cal.getCouplerForwardLossDbH());
    file.addGlobAttr("cal_coupler_forward_loss_db_v",
                     (float) cal.getCouplerForwardLossDbV());
    file.addGlobAttr("cal_test_power_dbm_h", (float) cal.getTestPowerDbmH());
    file.addGlobAttr("cal_test_power_dbm_v", (float) cal.getTestPowerDbmV());
    file.addGlobAttr("cal_zdr_correction_db", (float) cal.getZdrCorrectionDb());
    file.addGlobAttr("cal_ldr_correction_db_h", (float) cal.getLdrCorrectionDbH());
    file.addGlobAttr("cal_ldr_correction_db_v", (float) cal.getLdrCorrectionDbV());
    file.addGlobAttr("cal_phidp_rot_deg", (float) cal.getSystemPhidpDeg());

  } else {

    const ips_ts_calibration_t &cal = info.getCalibration();
    file.addGlobAttr("cal_wavelength_cm", cal.wavelength_cm);
    file.addGlobAttr("cal_beamwidth_deg_h", cal.beamwidth_deg_h);
    file.addGlobAttr("cal_beamwidth_deg_v", cal.beamwidth_deg_v);
    file.addGlobAttr("cal_gain_ant_db_h", cal.gain_ant_db_h);
    file.addGlobAttr("cal_gain_ant_db_v", cal.gain_ant_db_v);
    file.addGlobAttr("cal_pulse_width_us", cal.pulse_width_us);
    file.addGlobAttr("cal_xmit_power_dbm_h", cal.xmit_power_dbm_h);
    file.addGlobAttr("cal_xmit_power_dbm_v", cal.xmit_power_dbm_v);
    file.addGlobAttr("cal_two_way_waveguide_loss_db_h",
                     cal.two_way_waveguide_loss_db_h);
    file.addGlobAttr("cal_two_way_waveguide_loss_db_v",
                     cal.two_way_waveguide_loss_db_v);
    file.addGlobAttr("cal_two_way_radome_loss_db_h",
                     cal.two_way_radome_loss_db_h);
    file.addGlobAttr("cal_two_way_radome_loss_db_v",
                     cal.two_way_radome_loss_db_v);
    file.addGlobAttr("cal_receiver_mismatch_loss_db",
                     cal.receiver_mismatch_loss_db);
    file.addGlobAttr("cal_radar_constant_h", cal.radar_constant_h);
    file.addGlobAttr("cal_radar_constant_v", cal.radar_constant_v);
    file.addGlobAttr("cal_noise_dbm_hc", cal.noise_dbm_hc);
    file.addGlobAttr("cal_noise_dbm_hx", cal.noise_dbm_hx);
    file.addGlobAttr("cal_noise_dbm_vc", cal.noise_dbm_vc);
    file.addGlobAttr("cal_noise_dbm_vx", cal.noise_dbm_vx);
    file.addGlobAttr("cal_receiver_gain_db_hc", cal.receiver_gain_db_hc);
    file.addGlobAttr("cal_receiver_gain_db_hx", cal.receiver_gain_db_hx);
    file.addGlobAttr("cal_receiver_gain_db_vc", cal.receiver_gain_db_vc);
    file.addGlobAttr("cal_receiver_gain_db_vx", cal.receiver_gain_db_vx);
    file.addGlobAttr("cal_receiver_slope_hc", cal.receiver_slope_hc);
    file.addGlobAttr("cal_receiver_slope_hx", cal.receiver_slope_hx);
    file.addGlobAttr("cal_receiver_slope_vc", cal.receiver_slope_vc);
    file.addGlobAttr("cal_receiver_slope_vx", cal.receiver_slope_vx);
    file.addGlobAttr("cal_base_dbz_1km_hc", cal.base_dbz_1km_hc);
    file.addGlobAttr("cal_base_dbz_1km_hx", cal.base_dbz_1km_hx);
    file.addGlobAttr("cal_base_dbz_1km_vc", cal.base_dbz_1km_vc);
    file.addGlobAttr("cal_base_dbz_1km_vx", cal.base_dbz_1km_vx);
    file.addGlobAttr("cal_sun_power_dbm_hc", cal.sun_power_dbm_hc);
    file.addGlobAttr("cal_sun_power_dbm_hx", cal.sun_power_dbm_hx);
    file.addGlobAttr("cal_sun_power_dbm_vc", cal.sun_power_dbm_vc);
    file.addGlobAttr("cal_sun_power_dbm_vx", cal.sun_power_dbm_vx);
    file.addGlobAttr("cal_noise_source_power_dbm_h",
                     cal.noise_source_power_dbm_h);
    file.addGlobAttr("cal_noise_source_power_dbm_v",
                     cal.noise_source_power_dbm_v);
    file.addGlobAttr("cal_power_meas_loss_db_h", cal.power_meas_loss_db_h);
    file.addGlobAttr("cal_power_meas_loss_db_v", cal.power_meas_loss_db_v);
    file.addGlobAttr("cal_coupler_forward_loss_db_h",
                     cal.coupler_forward_loss_db_h);
    file.addGlobAttr("cal_coupler_forward_loss_db_v",
                     cal.coupler_forward_loss_db_v);
    file.addGlobAttr("cal_test_power_dbm_h", cal.test_power_dbm_h);
    file.addGlobAttr("cal_test_power_dbm_v", cal.test_power_dbm_v);
    file.addGlobAttr("cal_zdr_correction_db", cal.zdr_correction_db);
    file.addGlobAttr("cal_ldr_correction_db_h", cal.ldr_correction_db_h);
    file.addGlobAttr("cal_ldr_correction_db_v", cal.ldr_correction_db_v);
    file.addGlobAttr("cal_phidp_rot_deg", cal.phidp_rot_deg);

  }

}

////////////////////////////////////////
// write out base time variables
// Returns 0 on success, -1 on failure

int IpsTs2NetCDF::_writeBaseTimeVars(NcxxFile &file)
  
{
  
  // Base time - start time of data in secs

  char timeUnitsStr[256];
  DateTime stime(_startTime);
  sprintf(timeUnitsStr, "seconds since %.4d-%.2d-%.2dT%.2d:%.2d:%.2dZ",
          1970, 1, 1, 0, 0, 0);
  
  NcxxVar baseTimeVar;
  if (_addVar(file, baseTimeVar, ncxxDouble,
              "base_time", "time_since_Jan1_1970", timeUnitsStr)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeBaseTimeVars" << endl;
    cerr << "  Cannot create base_time var" << endl;
    return -1;
  }
  double baseTime = _startTime;
  baseTimeVar.putVal(baseTime);

  int year = stime.getYear();
  int month = stime.getMonth();
  int day = stime.getDay();
  int hour = stime.getHour();
  int min = stime.getMin();
  int sec = stime.getSec();

  NcxxVar baseYearVar;
  if (_addVar(file, baseYearVar, ncxxInt,
              "base_year", "base_time_year", "")) {
    cerr << "ERROR - IpsTs2NetCDF::_writeBaseTimeVars" << endl;
    cerr << "  Cannot create base_year var" << endl;
    return -1;
  }
  baseYearVar.putVal(year);

  NcxxVar baseMonthVar;
  if (_addVar(file, baseMonthVar, ncxxInt,
              "base_month", "base_time_month", "")) {
    cerr << "ERROR - IpsTs2NetCDF::_writeBaseTimeVars" << endl;
    cerr << "  Cannot create base_month var" << endl;
    return -1;
  }
  baseMonthVar.putVal(&month);

  NcxxVar baseDayVar;
  if (_addVar(file, baseDayVar, ncxxInt,
              "base_day", "base_time_day", "")) {
    cerr << "ERROR - IpsTs2NetCDF::_writeBaseTimeVars" << endl;
    cerr << "  Cannot create base_day var" << endl;
    return -1;
  }
  baseDayVar.putVal(&day);

  NcxxVar baseHourVar;
  if (_addVar(file, baseHourVar, ncxxInt,
              "base_hour", "base_time_hour", "")) {
    cerr << "ERROR - IpsTs2NetCDF::_writeBaseTimeVars" << endl;
    cerr << "  Cannot create base_hour var" << endl;
    return -1;
  }
  baseHourVar.putVal(&hour);

  NcxxVar baseMinVar;
  if (_addVar(file, baseMinVar, ncxxInt,
              "base_min", "base_time_min", "")) {
    cerr << "ERROR - IpsTs2NetCDF::_writeBaseTimeVars" << endl;
    cerr << "  Cannot create base_min var" << endl;
    return -1;
  }
  baseMinVar.putVal(&min);

  NcxxVar baseSecVar;
  if (_addVar(file, baseSecVar, ncxxInt,
              "base_sec", "base_time_sec", "")) {
    cerr << "ERROR - IpsTs2NetCDF::_writeBaseTimeVars" << endl;
    cerr << "  Cannot create base_sec var" << endl;
    return -1;
  }
  baseSecVar.putVal(&sec);

  return 0;

}  

////////////////////////////////////////
// write out time variables
// in ips format
// Returns 0 on success, -1 on failure

int IpsTs2NetCDF::_writeTimeDimVars(NcxxFile &file,
                                     NcxxDim &timeDim)
  
{
  
  // Time variable - secs since start of file

  char timeUnitsStr[256];
  DateTime stime(_startTime);
  sprintf(timeUnitsStr, "seconds since %.4d-%.2d-%.2dT%.2d:%.2d:%.2dZ",
          stime.getYear(), stime.getMonth(), stime.getDay(),
          stime.getHour(), stime.getMin(), stime.getSec());
  
  NcxxVar timeVar;
  if (_addVar(file, timeVar, ncxxDouble, timeDim,
              "time_offset", "time_offset_from_base_time", timeUnitsStr)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeTimeDimVars" << endl;
    cerr << "  Cannot create time var" << endl;
    return -1;
  }
  timeVar.addScalarAttr("_FillValue", -9999.0);
  TaArray<double> times_;
  double *times = times_.alloc(_nTimes);
  for (size_t jj = 0; jj < _nTimes; jj++) {
    times[jj] = _dtimeRay[jj];
  }
  timeVar.putVal(times);

  // ngates per ray

  if (_writeVar(file, timeDim,
                "n_gates_ray", "number_of_valid_gates_in_ray", "",
                _nGatesRay)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeTimeDimVars" << endl;
    return -1;
  }
  
  // Elevation variable

  if (_writeVar(file, timeDim,
                "elevation", "elevation_angle", "degrees",
                _elRay)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeTimeDimVars" << endl;
    return -1;
  }
  
  // Azimuth variable
  
  if (_writeVar(file, timeDim,
                "azimuth", "azimuth_angle", "degrees",
                _azRay)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeTimeDimVars" << endl;
    return -1;
  }
  
  // Fixed angle variable
  
  if (_writeVar(file, timeDim,
                "fixed_angle", "fixed_scan_angle", "degrees",
                _fixedAngleRay)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeTimeDimVars" << endl;
    return -1;
  }
  
  // PRT variable
  
  if (_writeVar(file, timeDim,
                "prt", "pulse_repetition_time", "seconds",
                _prtRay)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeTimeDimVars" << endl;
    return -1;
  }
  
  // Pulse width variable
  
  if (_writeVar(file, timeDim,
                "pulse_width", "pulse_width", "micro_seconds",
                _pulseWidthRay)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeTimeDimVars" << endl;
    return -1;
  }
  
  // Pulse sequence number
  
  if (_writeVar(file, timeDim,
                "pulse_seq_num", "pulse_seq_num", "",
                _pulseSeqNumRay)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeTimeDimVars" << endl;
    return -1;
  }
  
  // Dwell sequence number
  
  if (_writeVar(file, timeDim,
                "dwell_seq_num", "dwell_seq_num", "",
                _dwellSeqNumRay)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeTimeDimVars" << endl;
    return -1;
  }
  
  // beam num in dwell
  
  if (_writeVar(file, timeDim,
                "beam_num_in_dwell", "beam_num_in_dwell", "",
                _beamNumInDwellRay)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeTimeDimVars" << endl;
    return -1;
  }
  
  // visit num in beam
  
  if (_writeVar(file, timeDim,
                "visit_num_in_beam", "visit_num_in_beam", "",
                _visitNumInBeamRay)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeTimeDimVars" << endl;
    return -1;
  }
  
  // scan mode
  
  if (_writeVar(file, timeDim,
                "scan_mode", "scan_mode", "",
                _scanModeRay)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeTimeDimVars" << endl;
    return -1;
  }
  
  // sweep number
  
  if (_writeVar(file, timeDim,
                "sweep_num", "sweep_num", "",
                _sweepNumRay)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeTimeDimVars" << endl;
    return -1;
  }
  
  // volume number
  
  if (_writeVar(file, timeDim,
                "volume_num", "volume_num", "",
                _volNumRay)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeTimeDimVars" << endl;
    return -1;
  }
  
  // hv flag
  
  if (_writeVar(file, timeDim,
                "hv_flag", "hv_flag", "",
                _hvFlagRay)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeTimeDimVars" << endl;
    return -1;
  }
  
  // copol flag
  
  if (_writeVar(file, timeDim,
                "chan_is_copol", "chan_is_copol", "",
                _chanIsCopolRay)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeTimeDimVars" << endl;
    return -1;
  }
  
  return 0;

}

////////////////////////////////////////
// write out range coordinate variable
// Returns 0 on success, -1 on failure

int IpsTs2NetCDF::_writeRangeVar(NcxxFile &file,
                                  NcxxDim &gatesDim)
  
{
  
  NcxxVar rangeVar;
  if (_addVar(file, rangeVar, ncxxFloat, gatesDim,
              "range", "range_to_center_of_gate", "m")) {
    cerr << "ERROR - IpsTs2NetCDF::_writeRangeVar" << endl;
    cerr << "  Cannot create range var" << endl;
    return -1;
  }

  TaArray<float> range_;
  float *range = range_.alloc(_nGatesMax);
  double thisRange = _startRangeM;
  for (int ii = 0; ii < _nGatesMax; ii++, thisRange += _gateSpacingM) {
    range[ii] = thisRange;
  }
  rangeVar.putVal(range);

  return 0;

}

/////////////////////////////////
// compute output file path
//
// Returns 0 on success, -1 on failure

int IpsTs2NetCDF::_computeOutputFilePaths()

{

  DateTime stime(_startTime);
  
  char elevAngleStr[64];
  sprintf(elevAngleStr, "_%.2f", _startEl);

  char azAngleStr[64];
  sprintf(azAngleStr, "_%.2f", _startAz);
  
  char fixedAngleStr[64];
  sprintf(fixedAngleStr, "_%.2f", _startFixedAngle);
  
  string scanModeStr;
  switch (_startScanMode) {
    case ips_ts_scan_mode_t::CALIBRATION:
      scanModeStr = ".cal";
      break;
    case ips_ts_scan_mode_t::RHI:
      scanModeStr = ".rhi";
      break;
    case ips_ts_scan_mode_t::VPOINT:
      scanModeStr = ".vert";
      break;
    case ips_ts_scan_mode_t::IDLE:
      scanModeStr = ".idle";
      break;
    case ips_ts_scan_mode_t::SUNSCAN:
      scanModeStr = ".sun";
      break;
    case ips_ts_scan_mode_t::POINTING:
      scanModeStr = ".point";
      break;
    case ips_ts_scan_mode_t::PPI:
    default:
      scanModeStr = ".ppi";
      break;
  }
    
  // make the output dir

  char subDir[1024];
  sprintf(subDir, "%s/%.4d%.2d%.2d", _params.output_dir,
          stime.getYear(), stime.getMonth(), stime.getDay());
  
  if (ta_makedir_recurse(subDir)) {
    int errNum = errno;
    cerr << "ERROR - IpsTs2NetCDF" << endl;
    cerr << "  Cannot make output directory: " << subDir << endl;
    cerr << "  " << strerror(errNum) << endl;
    return -1;
  }
  
  // compute output path

  char relPath[BUFSIZ];

  if (_params.input_mode == Params::TS_FILE_INPUT &&
      _params.preserve_file_name) {
    Path path(_pulseReader->getPrevPathInUse());
    snprintf(relPath, BUFSIZ, "%s.nc", path.getFile().c_str());
  } else {
    if (_params.output_trigger == Params::END_OF_SWEEP) {
      snprintf(relPath, BUFSIZ, "%.4d%.2d%.2d_%.2d%.2d%.2d%s%s.nc",
               stime.getYear(), stime.getMonth(), stime.getDay(),
               stime.getHour(), stime.getMin(), stime.getSec(),
               fixedAngleStr, scanModeStr.c_str());
    } else {
      snprintf(relPath, BUFSIZ, "%.4d%.2d%.2d_%.2d%.2d%.2d%s%s%s.nc",
               stime.getYear(), stime.getMonth(), stime.getDay(),
               stime.getHour(), stime.getMin(), stime.getSec(),
               elevAngleStr, azAngleStr, scanModeStr.c_str());
    }
  }

  _outputName = relPath;

  _outputPath = subDir;
  _outputPath += PATH_DELIM;
  _outputPath += _outputName;

  _tmpPath = _outputPath;
  _tmpPath += ".tmp";
  
  return 0;

}

////////////////////////////////////////////////
// add string attribute to a variable

int IpsTs2NetCDF::_addAttr(NcxxVar &var,
                            const string &name,
                            const string &val)
  
{
  try {
    var.addScalarAttr(name , val);
  } catch (NcxxException& e) {
    cerr << "ERROR - IpsTs2NetCDF::_addAttr" << endl;
    cerr << "  Cannot add attr name: " << name << endl;
    cerr << "  val: " << val << endl;
    cerr << "  Variable name: " << var.getName() << endl;
    cerr << "  exception: " << e.what() << endl;
    return -1;
  }
  return 0;
}

////////////////////////////////////////////////
// add double attribute to a variable

int IpsTs2NetCDF::_addAttr(NcxxVar & var,
                            const string &name,
                            double val)

{
  try {
    var.addScalarAttr(name , val);
  } catch (NcxxException& e) {
    cerr << "ERROR - IpsTs2NetCDF::_addAttr" << endl;
    cerr << "  Cannot add attr name: " << name << endl;
    cerr << "  val: " << val << endl;
    cerr << "  Variable name: " << var.getName() << endl;
    cerr << "  exception: " << e.what() << endl;
    return -1;
  }
  return 0;
}

////////////////////////////////////////////////
// add int attribute to a variable

int IpsTs2NetCDF::_addAttr(NcxxVar &var,
                            const string &name,
                            int val)
  
{
  try {
    var.addScalarAttr(name , val);
  } catch (NcxxException& e) {
    cerr << "ERROR - IpsTs2NetCDF::_addAttr" << endl;
    cerr << "  Cannot add attr name: " << name << endl;
    cerr << "  val: " << val << endl;
    cerr << "  Variable name: " << var.getName() << endl;
    cerr << "  exception: " << e.what() << endl;
    return -1;
  }
  return 0;
}

//////////////////////////////////////////////
// add scalar variable

int IpsTs2NetCDF::_addVar(NcxxFile &file,
                           NcxxVar &var,
                           NcxxType ncType,
                           const string &name,
                           const string &standardName,
                           const string &units /* = "" */)

{

  try {
    var = file.addVar(name,
                      standardName,
                      standardName,
                      ncType,
                      units);
  } catch (NcxxException& e) {
    cerr << "ERROR - IpsTs2NetCDF::_addVar" << endl;
    cerr << "  Cannot add scalar var, name: " << name << endl;
    cerr << "  Type: " << _ncTypeToStr(ncType) << endl;
    cerr << "  exception: " << e.what() << endl;
    return -1;
  }
  
  return 0;
  
}

//////////////////////////////////////////////
// add a 1-D variable

int IpsTs2NetCDF::_addVar(NcxxFile &file,
                           NcxxVar &var,
                           NcxxType ncType,
                           NcxxDim &dim, 
                           const string &name,
                           const string &standardName,
                           const string &units /* = "" */)

{
  
  try {
    var = file.addVar(name,
                      standardName,
                      standardName,
                      ncType,
                      dim,
                      units);
  } catch (NcxxException& e) {
    cerr << "ERROR - IpsTs2NetCDF::_addVar" << endl;
    cerr << "  Cannot add 1-D var, name: " << name << endl;
    cerr << "  Type: " << _ncTypeToStr(ncType) << endl;
    cerr << "  Dim: " << dim.getName() << endl;
    cerr << "  exception: " << e.what() << endl;
    return -1;
  }
  
  return 0;

}

//////////////////////////////////////////////
// add a 2-D variable

int IpsTs2NetCDF::_addVar(NcxxFile &file,
                           NcxxVar &var,
                           NcxxType ncType,
                           NcxxDim &dim0, 
                           NcxxDim &dim1, 
                           const string &name,
                           const string &standardName,
                           const string &units /* = "" */)

{
  
  try {
    var = file.addVar(name,
                      standardName,
                      standardName,
                      ncType,
                      dim0,
                      dim1,
                      units);
  } catch (NcxxException& e) {
    cerr << "ERROR - IpsTs2NetCDF::_addVar" << endl;
    cerr << "  Cannot add 2-D var, name: " << name << endl;
    cerr << "  Type: " << _ncTypeToStr(ncType) << endl;
    cerr << "  Dim0: " << dim0.getName() << endl;
    cerr << "  Dim1: " << dim1.getName() << endl;
    cerr << "  exception: " << e.what() << endl;
    return -1;
  }

  return 0;

}

////////////////////////////////////////
// add and write float var
// Returns 0 on success, -1 on failure

int IpsTs2NetCDF::_writeVar(NcxxFile &file,
                             NcxxDim &timeDim,
                             const string &name,
                             const string &standardName,
                             const string &units,
                             const vector<float> vals)
  
{
  
  TaArray<float> floats_;
  float *floats = floats_.alloc(timeDim.getSize());
  
  NcxxVar var;
  if (_addVar(file, var, ncxxFloat, timeDim,
              name, standardName, units)) {
    cerr << "ERROR - IpsTs2NetCDF::_addFloatVar" << endl;
    cerr << "  Cannot create var, name: " << name << endl;
    return -1;
  }
  var.addScalarAttr("_FillValue", -9999.0f);
  for (size_t jj = 0; jj < timeDim.getSize(); jj++) {
    floats[jj] = vals[jj];
  }
  var.putVal(floats);
  
  return 0;

}

////////////////////////////////////////
// add and write int var
// Returns 0 on success, -1 on failure

int IpsTs2NetCDF::_writeVar(NcxxFile &file,
                             NcxxDim &timeDim,
                             const string &name,
                             const string &standardName,
                             const string &units,
                             const vector<int> vals)
  
{
  
  TaArray<int> ints_;
  int *ints = ints_.alloc(timeDim.getSize());
  
  NcxxVar var;
  if (_addVar(file, var, ncxxInt, timeDim,
              name, standardName, units)) {
    cerr << "ERROR - IpsTs2NetCDF::_addIntVar" << endl;
    cerr << "  Cannot create var, name: " << name << endl;
    return -1;
  }
  var.addScalarAttr("_FillValue", -9999);
  for (size_t jj = 0; jj < timeDim.getSize(); jj++) {
    ints[jj] = vals[jj];
  }
  var.putVal(ints);
  
  return 0;

}

////////////////////////////////////////
// add and write int64 var
// Returns 0 on success, -1 on failure

int IpsTs2NetCDF::_writeVar(NcxxFile &file,
                             NcxxDim &timeDim,
                             const string &name,
                             const string &standardName,
                             const string &units,
                             const vector<int64_t> vals)
  
{
  
  TaArray<int64_t> ints_;
  int64_t *ints = ints_.alloc(timeDim.getSize());
  
  NcxxVar var;
  if (_addVar(file, var, ncxxInt64, timeDim,
              name, standardName, units)) {
    cerr << "ERROR - IpsTs2NetCDF::_writeVar" << endl;
    cerr << "  Cannot create var, name: " << name << endl;
    return -1;
  }
  var.addScalarAttr("_FillValue", (int64_t) -9999);
  for (size_t jj = 0; jj < timeDim.getSize(); jj++) {
    ints[jj] = vals[jj];
  }
  var.putVal(ints);
  
  return 0;

}

////////////////////////////////////////
// add and write IQ vars
// Returns 0 on success, -1 on failure

int IpsTs2NetCDF::_writeIqVars(NcxxFile &file,
                                NcxxDim &timeDim,
                                NcxxDim &gatesDim,
                                NcxxDim &rowsElementsDim,
                                const string &iName,
                                const string &qName,
                                const float *ivals,
                                const float *qvals)
  
{

  // I variable
  
  NcxxVar iVar = file.addVar(iName,
                             "time_series_in_phase",
                             "time_series_in_phase",
                             ncxxFloat,
                             timeDim,
                             gatesDim,
                             rowsElementsDim,
                             "scaled A/D counts");
  iVar.addScalarAttr("_FillValue", (float) -9999.0);
  iVar.putVal(ivals);

  // Q variable

  NcxxVar qVar = file.addVar(qName,
                             "time_series_quadrature",
                             "time_series_quadrature",
                             ncxxFloat,
                             timeDim,
                             gatesDim,
                             rowsElementsDim,
                             "scaled A/D counts");
  qVar.addScalarAttr("_FillValue", (float) -9999.0);
  qVar.putVal(qvals);

  return 0;

}

////////////////////////////////////////
// convert enums to strings

string IpsTs2NetCDF::_ncTypeToStr(NcxxType nctype)
  
{
  return nctype.getTypeClassName();
}

