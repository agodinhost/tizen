/*!
** \file    location_manager.c
** \date    2014/12/17 08:00
** \brief   location manager controller, definition.
** \author  A.Godinho (Woody)
**/

#ifndef __location_manager_H__
#define __location_manager_H__

#define REMOTE_APP_ID   "org.gec.gpsViewer"
#define REMOTE_PORT     "gps.port"

/*
 * Initialize the location manager service.
 */
bool locationInitialize();

/*
 * Stop the location manager service.
 */
void locationStop();

/*
 * Finalize the location manager service.
 */
void locationFinalize();

#endif /* __location_manager_H__ */

