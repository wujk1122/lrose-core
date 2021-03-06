/* *=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=* */
/* ** Copyright UCAR (c) 1990 - 2016                                         */
/* ** University Corporation for Atmospheric Research (UCAR)                 */
/* ** National Center for Atmospheric Research (NCAR)                        */
/* ** Boulder, Colorado, USA                                                 */
/* ** BSD licence applies - redistribution and use in source and binary      */
/* ** forms, with or without modification, are permitted provided that       */
/* ** the following conditions are met:                                      */
/* ** 1) If the software is modified to produce derivative works,            */
/* ** such modified software should be clearly marked, so as not             */
/* ** to confuse it with the version available from UCAR.                    */
/* ** 2) Redistributions of source code must retain the above copyright      */
/* ** notice, this list of conditions and the following disclaimer.          */
/* ** 3) Redistributions in binary form must reproduce the above copyright   */
/* ** notice, this list of conditions and the following disclaimer in the    */
/* ** documentation and/or other materials provided with the distribution.   */
/* ** 4) Neither the name of UCAR nor the names of its contributors,         */
/* ** if any, may be used to endorse or promote products derived from        */
/* ** this software without specific prior written permission.               */
/* ** DISCLAIMER: THIS SOFTWARE IS PROVIDED "AS IS" AND WITHOUT ANY EXPRESS  */
/* ** OR IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED      */
/* ** WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.    */
/* *=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=* */
/*
 * pos_gui_ui.c - User interface object initialization functions.
 * This file was generated by `gxv' from `pos_gui.G'.
 * DO NOT EDIT BY HAND.
 */

#include <stdio.h>
#include <sys/param.h>
#include <sys/types.h>
#include <xview/xview.h>
#include <xview/canvas.h>
#include <xview/panel.h>
#include <xview/scrollbar.h>
#include <xview/svrimage.h>
#include <xview/termsw.h>
#include <xview/text.h>
#include <xview/tty.h>
#include <xview/xv_xrect.h>
#include "pos_gui_ui.h"

/*
 * Initialize an instance of object `main_bw'.
 */
pos_gui_main_bw_objects *
pos_gui_main_bw_objects_initialize(ip, owner)
	pos_gui_main_bw_objects	*ip;
	Xv_opaque	owner;
{
	if (!ip && !(ip = (pos_gui_main_bw_objects *) calloc(1, sizeof (pos_gui_main_bw_objects))))
		return (pos_gui_main_bw_objects *) NULL;
	if (!ip->main_bw)
		ip->main_bw = pos_gui_main_bw_main_bw_create(ip, owner);
	if (!ip->main_cp)
		ip->main_cp = pos_gui_main_bw_main_cp_create(ip, ip->main_bw);
	if (!ip->label_msg)
		ip->label_msg = pos_gui_main_bw_label_msg_create(ip, ip->main_cp);
	if (!ip->radial_msg)
		ip->radial_msg = pos_gui_main_bw_radial_msg_create(ip, ip->main_cp);
	if (!ip->km_msg)
		ip->km_msg = pos_gui_main_bw_km_msg_create(ip, ip->main_cp);
	if (!ip->origin_lst)
		ip->origin_lst = pos_gui_main_bw_origin_lst_create(ip, ip->main_cp);
	return ip;
}

/*
 * Create object `main_bw' in the specified instance.
 */
Xv_opaque
pos_gui_main_bw_main_bw_create(ip, owner)
	pos_gui_main_bw_objects	*ip;
	Xv_opaque	owner;
{
	Xv_opaque	obj;
	Xv_opaque		main_bw_image;
	static unsigned short	main_bw_bits[] = {
#include "pos_report.icon"
	};
	
	main_bw_image = xv_create(XV_NULL, SERVER_IMAGE,
		SERVER_IMAGE_DEPTH, 1,
		SERVER_IMAGE_BITS, main_bw_bits,
		XV_WIDTH, 64,
		XV_HEIGHT, 64,
		NULL);
	obj = xv_create(owner, FRAME,
		XV_KEY_DATA, INSTANCE, ip,
		XV_WIDTH, 167,
		XV_HEIGHT, 207,
		XV_LABEL, "Position Report",
		FRAME_SHOW_FOOTER, FALSE,
		FRAME_SHOW_RESIZE_CORNER, FALSE,
		FRAME_ICON, xv_create(XV_NULL, ICON,
			ICON_IMAGE, main_bw_image,
			NULL),
		NULL);
	return obj;
}

/*
 * Create object `main_cp' in the specified instance.
 */
Xv_opaque
pos_gui_main_bw_main_cp_create(ip, owner)
	pos_gui_main_bw_objects	*ip;
	Xv_opaque	owner;
{
	Xv_opaque	obj;
	
	obj = xv_create(owner, PANEL,
		XV_KEY_DATA, INSTANCE, ip,
		XV_X, 0,
		XV_Y, 0,
		XV_WIDTH, WIN_EXTEND_TO_EDGE,
		XV_HEIGHT, WIN_EXTEND_TO_EDGE,
		WIN_BORDER, FALSE,
		NULL);
	return obj;
}

/*
 * Create object `label_msg' in the specified instance.
 */
Xv_opaque
pos_gui_main_bw_label_msg_create(ip, owner)
	pos_gui_main_bw_objects	*ip;
	Xv_opaque	owner;
{
	Xv_opaque	obj;
	
	obj = xv_create(owner, PANEL_MESSAGE,
		XV_KEY_DATA, INSTANCE, ip,
		XV_X, 12,
		XV_Y, 6,
		PANEL_LABEL_STRING, "ORIGIN is:",
		PANEL_LABEL_BOLD, TRUE,
		NULL);
	return obj;
}

/*
 * Create object `radial_msg' in the specified instance.
 */
Xv_opaque
pos_gui_main_bw_radial_msg_create(ip, owner)
	pos_gui_main_bw_objects	*ip;
	Xv_opaque	owner;
{
	Xv_opaque	obj;
	
	obj = xv_create(owner, PANEL_MESSAGE,
		XV_KEY_DATA, INSTANCE, ip,
		XV_X, 12,
		XV_Y, 24,
		PANEL_LABEL_STRING, "0.0 km Rad.   000 deg",
		PANEL_LABEL_BOLD, TRUE,
		NULL);
	return obj;
}

/*
 * Create object `km_msg' in the specified instance.
 */
Xv_opaque
pos_gui_main_bw_km_msg_create(ip, owner)
	pos_gui_main_bw_objects	*ip;
	Xv_opaque	owner;
{
	Xv_opaque	obj;
	
	obj = xv_create(owner, PANEL_MESSAGE,
		XV_KEY_DATA, INSTANCE, ip,
		XV_X, 12,
		XV_Y, 40,
		PANEL_LABEL_STRING, "0.0 km E, 0.0 km N",
		PANEL_LABEL_BOLD, TRUE,
		NULL);
	return obj;
}

/*
 * Create object `origin_lst' in the specified instance.
 */
Xv_opaque
pos_gui_main_bw_origin_lst_create(ip, owner)
	pos_gui_main_bw_objects	*ip;
	Xv_opaque	owner;
{
	extern int		origin_select_proc();
	Xv_opaque	obj;
	
	obj = xv_create(owner, PANEL_LIST,
		XV_KEY_DATA, INSTANCE, ip,
		XV_X, 4,
		XV_Y, 58,
		PANEL_LIST_WIDTH, 140,
		PANEL_LIST_DISPLAY_ROWS, 7,
		PANEL_LAYOUT, PANEL_VERTICAL,
		PANEL_READ_ONLY, TRUE,
		PANEL_CHOOSE_ONE, TRUE,
		PANEL_CHOOSE_NONE, TRUE,
		PANEL_NOTIFY_PROC, origin_select_proc,
		NULL);
	return obj;
}

