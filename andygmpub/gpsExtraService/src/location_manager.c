/*!
** \file    location_manager.c
** \date    2014/12/17 08:00
** \brief   location manager controller, implementation.
** \author  A.Godinho (Woody)
**
** \version history
**          04 Mar 2017 - first version.
**
** \model   https://developer.tizen.org/ko/development/api-tutorials/native-application/location/location-manager?langswitch=ko
** \more    https://developer.tizen.org/ko/development/guides/native-application/location-and-sensors/location-information?langredirect=1
**          https://developer.tizen.org/dev-guide/native/2.3.0/org.tizen.mobile.native.apireference/group__CAPI__LOCATION__MANAGER__MODULE.html
**/

#include <tizen.h>
#include <stdio.h>
#include <bundle.h>
#include <locations.h>
#include <message_port.h>
#include <efl_extension.h>
#include <runtime_info.h>

#include "location_manager.h"
#include "andygmpub-gps.h"

#define POSITION_UPDATE_INTERVAL 		3       /* original value was 2. */
#define SATELLITE_UPDATE_INTERVAL 		10      /* original value was 5 secs. */
#define CHAR_BUFF_SIZE 				20
#define MAX_TIME_DIFF 				30      /* original value was 15 secs. */
#define SEND_DATA_INTERVAL 			5.0     /* original value was 5.0. */

#define MESSAGE_TYPE_POSITION_UPDATE 	        "POSITION_UPDATE"
#define MESSAGE_TYPE_SATELLITES_UPDATE	        "SATELLITES_UPDATE"

static struct
{
   bool                      gpsEnabled;
   bool                      satEnabled;
   location_manager_h        manager;
   location_service_state_e  state;
   bool                      connected;
   bool                      data_sent;
}
   s_location_data =
{
   .gpsEnabled = false,
   .satEnabled = false,
   .manager    = NULL,
   .state      = LOCATIONS_SERVICE_DISABLED,
   .connected  = false,
   .data_sent  = true
};

static struct
{
   double altitude;
   double latitude;
   double longitude;
   double climb;
   double direction;
   double speed;
   double horizontal;
   double vertical;
   location_accuracy_level_e level;
   time_t timestamp;
}
   s_gps_data =
{
   .altitude   = 0.0,
   .latitude   = 0.0,
   .longitude  = 0.0,
   .climb      = 0.0,
   .direction  = 0.0,
   .speed      = 0.0,
   .horizontal = 0.0,
   .vertical   = 0.0,
   .level      = 0,
   .timestamp  = (time_t)0
};

static struct
{
   int    active;
   int    inview;
   time_t timestamp;
}
   s_sat_data =
{
   .active    = 0,
   .inview    = 0,
   .timestamp = (time_t)0
};

static void
   checkRemotePort();
static bool
   gpsConnected();
static bool
   sendMessage( bundle *b );
static bool
   sendPosition();
static bool
   sendSatellite();

static bool
   enableGPS();
static bool
   enableSat();
static void
   disableGPS();
static void
   disableSat();

static void
   onStateChange( location_service_state_e state, void *user_data );
static void
   onPositionChange( double latitude, double longitude, double altitude, time_t timestamp, void *data );
static bool
   onSatelliteData( unsigned int azimuth, unsigned int elevation, unsigned int prn, int snr, bool inUse, void *user_data );
static void
   onSatelliteChange( int num_active, int num_inview, time_t timestamp, void *data );
static Eina_Bool
   onTimerSend( void *data );

static Eina_Bool
   initDataSend();
static bool
   initData();
static void
   logNMEA( int samples );

// ------------------------------------------------------------------------------------------------
// Public methods.
// ------------------------------------------------------------------------------------------------

/*!
 * Initialize the location manager.
 */
bool
locationInitialize()
{
   /* 1. Log the GPS status. */
   gpsConnected();

   /* 2. Create the location manager handle. */
   location_error_e
   ret = location_manager_create( LOCATIONS_METHOD_GPS, &s_location_data.manager );
   if( ret != LOCATIONS_ERROR_NONE )
   {
      dlog_print( DLOG_ERROR, LOG_TAG, "locationInitialize# location manager creation error [%d,%s].", ret, get_error_message( ret ) );
      return false;
   }

   /* 3. Register the callback for status change. */
   ret = location_manager_set_service_state_changed_cb(
      s_location_data.manager,
      onStateChange,
      NULL
   );
   if( ret != LOCATIONS_ERROR_NONE )
   {
      dlog_print( DLOG_ERROR, LOG_TAG, "locationInitialize# status change register error [%d,%s].", ret, get_error_message( ret ) );
      locationFinalize();
      return false;
   }

   /* 4. Start location service. */
   location_manager_start( s_location_data.manager );

   /* 5. Check the remote port. */
   checkRemotePort();

   return true;
}

/*!
 * Stop the location manager.
 */
void
locationStop()
{
   locationFinalize();
}

/*!
 * Finalize the location manager.
 */
void
locationFinalize()
{
   disableSat();
   disableGPS();
   if( s_location_data.manager )
   {
      location_manager_unset_service_state_changed_cb( s_location_data.manager );
      location_manager_stop( s_location_data.manager );
      location_manager_destroy( s_location_data.manager );
   }
   s_location_data.manager = NULL;
}

// ------------------------------------------------------------------------------------------------
// Private methods.
// ------------------------------------------------------------------------------------------------

static bool
enableGPS()
{
   /* 1. gps callback already initialized. */
   if( s_location_data.gpsEnabled )
      return true;

   /* 2. Register the callback for position update. */
   location_error_e
   ret = location_manager_set_position_updated_cb(
      s_location_data.manager,
      onPositionChange,
      POSITION_UPDATE_INTERVAL,
      NULL
   );
   if( ret != LOCATIONS_ERROR_NONE )
   {
      dlog_print( DLOG_ERROR, LOG_TAG, "enabledGPS# position update register error [%d,%s].", ret, get_error_message( ret ) );
      disableGPS();
      return false;
   }

   /* 3. TRY to send the initial data to the remote port. */
   if( !initDataSend() )
   {
      dlog_print( DLOG_ERROR, LOG_TAG, "enabledGPS# init data send error, timer enabled." );
      ecore_timer_add( SEND_DATA_INTERVAL, onTimerSend, NULL );
   }

   s_location_data.gpsEnabled = true;
   return true;
}

static bool
enableSat()
{
   /* 1. Satellite callback already initialized. */
   if( s_location_data.satEnabled )
      return true;

   /* 2. Register the callback for satellites data update. */
   location_error_e
   ret = gps_status_set_satellite_updated_cb(
      s_location_data.manager,
      onSatelliteChange,
      SATELLITE_UPDATE_INTERVAL,
      NULL
   );
   if( ret != LOCATIONS_ERROR_NONE )
   {
      dlog_print( DLOG_ERROR, LOG_TAG, "enableSat# satellite update register error [%d,%s].", ret, get_error_message( ret ) );
      disableSat();
      return false;
   }

   s_location_data.satEnabled = true;
   return true;
}

static void
disableGPS()
{
   if( s_location_data.manager )
      location_manager_unset_position_updated_cb( s_location_data.manager );
   s_location_data.gpsEnabled = false;
}

static void
disableSat()
{
   if( s_location_data.manager )
      gps_status_unset_satellite_updated_cb( s_location_data.manager );
   s_location_data.satEnabled = false;
}

static bool
gpsConnected()
{
   bool locationEnaled, wifiEnabled, bluetoothEnabled;
   int gpsStatus;

   int
   ret = runtime_info_get_value_bool( RUNTIME_INFO_KEY_LOCATION_SERVICE_ENABLED, &locationEnaled );
   if( ret != RUNTIME_INFO_ERROR_NONE )
      dlog_print( DLOG_ERROR, LOG_TAG, "logServices# location status error [%d,%s].", ret, get_error_message( ret ) );

   ret = runtime_info_get_value_int( RUNTIME_INFO_KEY_GPS_STATUS, &gpsStatus );
   if( ret != RUNTIME_INFO_ERROR_NONE )
      dlog_print( DLOG_ERROR, LOG_TAG, "logServices# GPS status error [%d,%s].", ret, get_error_message( ret ) );

   ret = runtime_info_get_value_bool( RUNTIME_INFO_KEY_WIFI_HOTSPOT_ENABLED, &wifiEnabled );
   if( ret != RUNTIME_INFO_ERROR_NONE )
      dlog_print( DLOG_ERROR, LOG_TAG, "logServices# WiFi status error [%d,%s].", ret, get_error_message( ret ) );

   ret = runtime_info_get_value_bool( RUNTIME_INFO_KEY_BLUETOOTH_ENABLED, &bluetoothEnabled );
   if( ret != RUNTIME_INFO_ERROR_NONE )
      dlog_print( DLOG_ERROR, LOG_TAG, "logServices# Bluetooth status error [%d,%s].", ret, get_error_message( ret ) );

   dlog_print( DLOG_DEBUG, LOG_TAG, "logServices# location %d, gpsStatus %d, wifi %d, bluetooth %d.",
      locationEnaled,
      gpsStatus,
      wifiEnabled,
      bluetoothEnabled
   );

   return gpsStatus == RUNTIME_INFO_GPS_STATUS_CONNECTED;
}

static void
checkRemotePort()
{
   int ret = message_port_check_remote_port( REMOTE_APP_ID, REMOTE_PORT, &s_location_data.connected );
   if( ret != MESSAGE_PORT_ERROR_NONE )
      dlog_print( DLOG_ERROR, LOG_TAG, "checkRemotePort# error [%d,%s].", ret, get_error_message( ret ) );
}

/*!
 * Send message to the remote app port.
 */
static bool
sendMessage( bundle *b )
{
   if( !s_location_data.connected )
      return true;

   bool sent = false;

   if( b )
   {
      int ret = message_port_send_message( REMOTE_APP_ID, REMOTE_PORT, b );
      sent = ( ret == MESSAGE_PORT_ERROR_NONE );
      if( sent )
         dlog_print( DLOG_DEBUG, LOG_TAG, "sendMessage# message sent." );
      else
         dlog_print( DLOG_ERROR, LOG_TAG, "sendMessage# error [%d,%s].", ret, get_error_message( ret ) );
   }
   else
      dlog_print( DLOG_ERROR, LOG_TAG, "sendMessage# invalid bundle." );

   return sent;
}

static bool
sendPosition()
{
   bool ret = false;
   bundle *b = bundle_create();
   if( b )
   {
      char latitude_str[ CHAR_BUFF_SIZE ], longitude_str[ CHAR_BUFF_SIZE ], altitude_str[ CHAR_BUFF_SIZE ];

      snprintf( latitude_str,  CHAR_BUFF_SIZE, "%f", s_gps_data.latitude  );
      snprintf( longitude_str, CHAR_BUFF_SIZE, "%f", s_gps_data.longitude );
      snprintf( altitude_str,  CHAR_BUFF_SIZE, "%f", s_gps_data.altitude  );

      bundle_add_str( b, "msg_type",  MESSAGE_TYPE_POSITION_UPDATE );
      bundle_add_str( b, "latitude",  latitude_str );
      bundle_add_str( b, "longitude", longitude_str );
      bundle_add_str( b, "altitude",  longitude_str );

      ret = sendMessage( b );

      bundle_free( b );
   }
   else
      dlog_print( DLOG_ERROR, LOG_TAG, "sendPosition# invalid bundle." );

   return ret;
}

static bool
sendSatellite()
{
   bool ret = false;
   bundle *b = bundle_create();
   if( b )
   {
      char active_str[ CHAR_BUFF_SIZE ], inview_str[ CHAR_BUFF_SIZE ];

      snprintf( active_str, CHAR_BUFF_SIZE, "%d", s_sat_data.active );
      snprintf( inview_str, CHAR_BUFF_SIZE, "%d", s_sat_data.inview );

      bundle_add_str( b, "msg_type", MESSAGE_TYPE_SATELLITES_UPDATE );
      bundle_add_str( b, "active", active_str );
      bundle_add_str( b, "inview", inview_str );

      ret = sendMessage( b );

      bundle_free( b );
   }
   else
      dlog_print( DLOG_ERROR, LOG_TAG, "sendSatellite# invalid bundle." );

   return ret;
}

static void
onStateChange( location_service_state_e state, void *user_data )
{
   s_location_data.state = state;

   if( state == LOCATIONS_SERVICE_ENABLED )
   {
      enableGPS();
      if( gpsConnected() )
         enableSat();

      location_error_e
      ret = location_manager_get_location(
         s_location_data.manager,
         &s_gps_data.altitude,  &s_gps_data.latitude,    &s_gps_data.longitude,
         &s_gps_data.climb,     &s_gps_data.direction,   &s_gps_data.speed,
         &s_gps_data.level,     &s_gps_data.horizontal,  &s_gps_data.vertical,
         &s_gps_data.timestamp
      );
      if( ret != LOCATIONS_ERROR_NONE )
         dlog_print( DLOG_ERROR, LOG_TAG, "onStateChange# get_location error [%d,%s].", ret, get_error_message( ret ) );
      dlog_print( DLOG_INFO, LOG_TAG, "onStateChange# location data: Al%f Lt%f Lg%f Cl%f Dr%f Sp%f Lv%f Hr%f Vr%f.",
         s_gps_data.altitude,   s_gps_data.latitude,     s_gps_data.longitude,
         s_gps_data.climb,      s_gps_data.direction,    s_gps_data.speed,
         s_gps_data.level,      s_gps_data.horizontal,   s_gps_data.vertical
      );

      ret = gps_status_get_satellite(
         s_location_data.manager,
         &s_sat_data.active,
         &s_sat_data.inview,
         &s_sat_data.timestamp
      );
      if( ret != LOCATIONS_ERROR_NONE )
         dlog_print( DLOG_ERROR, LOG_TAG, "onStateChange# get_satellite error [%d,%s].", ret, get_error_message( ret ) );
      dlog_print( DLOG_INFO, LOG_TAG, "onStateChange# satellite data: active [%d] in view: [%d].",
         s_sat_data.active,
         s_sat_data.inview
      );

      logNMEA( 1 );
   }
   else if( state == LOCATIONS_SERVICE_DISABLED )
   {
      disableSat();
      disableGPS();
   }
}

static void
onPositionChange( double latitude, double longitude, double altitude, time_t timestamp, void *data )
{
   s_gps_data.latitude  = latitude;
   s_gps_data.longitude = longitude;
   s_gps_data.altitude  = altitude;
   s_gps_data.timestamp = timestamp;

   time_t curr_timestamp;
   time( &curr_timestamp );

   if( gpsConnected() )
      enableSat();

   if( s_location_data.data_sent && curr_timestamp - timestamp < MAX_TIME_DIFF )
   {
      if( sendPosition() )
         dlog_print( DLOG_INFO, LOG_TAG, "onPositionChange# Lt %f, Lg %f, Al %f.", latitude, longitude, altitude );
      else
         dlog_print( DLOG_ERROR, LOG_TAG, "onPositionChange# Failed to send." );
   }
}

static bool
onSatelliteData( unsigned int azimuth, unsigned int elevation, unsigned int prn, int snr, bool inUse, void *user_data )
{
   dlog_print( DLOG_DEBUG, LOG_TAG, "onSatelliteData# Azimuth %d Elevation %d, prn %d, snr %d InUse %d.",
      azimuth,
      elevation,
      prn,
      snr,
      inUse
   );
   return true;
}

static void
onSatelliteChange( int num_active, int num_inview, time_t timestamp, void *data )
{
   s_sat_data.active    = num_active;
   s_sat_data.inview    = num_inview;
   s_sat_data.timestamp = timestamp;

   if( num_inview > 0 )
      gps_status_foreach_satellites_in_view( s_location_data.manager, onSatelliteData, NULL );

   if( s_location_data.data_sent )
   {
      if( sendSatellite() )
         dlog_print( DLOG_INFO, LOG_TAG, "onSatelliteChange# active %d, in view %d.", num_active, num_inview );
      else
         dlog_print( DLOG_ERROR, LOG_TAG, "onSatelliteChange# Failed to send." );
   }
}

static Eina_Bool
onTimerSend( void *data )
{
   if( initDataSend() )
      return ECORE_CALLBACK_CANCEL;
   else
      return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
initDataSend( void )
{
   /* Get initial position and satellites count. */
   if( !initData() )
   {
      dlog_print( DLOG_ERROR, LOG_TAG, "initDataSend# Failed to initialize location data." );
      return EINA_FALSE;
   }

   /* Send initial data to consumer application. */
   if( !sendSatellite() || !sendPosition() )
   {
      dlog_print( DLOG_ERROR, LOG_TAG, "initDataSend# Failed to send location data." );
      return EINA_FALSE;
   }

   s_location_data.data_sent = true;

   return EINA_TRUE;
}

static bool
initData()
{
   /* Get last location information. */
   location_error_e
   ret = location_manager_get_last_location(
      s_location_data.manager,
      &s_gps_data.altitude,  &s_gps_data.latitude,    &s_gps_data.longitude,
      &s_gps_data.climb,     &s_gps_data.direction,   &s_gps_data.speed,
      &s_gps_data.level,     &s_gps_data.horizontal,  &s_gps_data.vertical,
      &s_gps_data.timestamp
   );
   if( ret == LOCATIONS_ERROR_NONE )
      dlog_print( DLOG_INFO, LOG_TAG, "initData# location data: Al%f Lt%f Lg%f Cl%f Dr%f Sp%f Lv%f Hr%f Vr%f.",
         s_gps_data.altitude,   s_gps_data.latitude,     s_gps_data.longitude,
         s_gps_data.climb,      s_gps_data.direction,    s_gps_data.speed,
         s_gps_data.level,      s_gps_data.horizontal,   s_gps_data.vertical
      );
   else
      dlog_print( DLOG_ERROR, LOG_TAG, "initData# last location error [%d,%s].", ret, get_error_message( ret ) );

   /* Get current time and compare it to the last timestamp. */
   time_t cur_timestamp;
   time( &cur_timestamp );

   if( cur_timestamp - s_gps_data.timestamp > MAX_TIME_DIFF )
   {
      dlog_print( DLOG_ERROR, LOG_TAG, "initData# last location expired." );
      return false;
   }

   /* Get last satellites information. */
   ret = gps_status_get_last_satellite(
      s_location_data.manager,
      &s_sat_data.active,
      &s_sat_data.inview,
      &s_sat_data.timestamp
   );
   if( ret == LOCATIONS_ERROR_NONE )
      dlog_print( DLOG_INFO, LOG_TAG, "initData# satellite data: active [%d] in view: [%d].",
         s_sat_data.active,
         s_sat_data.inview
      );
   else
      dlog_print( DLOG_ERROR, LOG_TAG, "initData# satellite status error [%d,%s].", ret, get_error_message( ret ) );

   if( s_sat_data.inview > 0 )
      gps_status_foreach_satellites_in_view( s_location_data.manager, onSatelliteData, NULL );

   return true;
}


static void
logNMEA( int samples )
{
   int nmea_len = 1024 * sizeof( char );
   char *nmea = malloc( nmea_len );

   for( int i = 0; i < samples; i ++ )
   {
      memset( nmea, 0, nmea_len );

      int ret = gps_status_get_nmea( s_location_data.manager, &nmea );
      if( ret == LOCATIONS_ERROR_NONE )
         dlog_print( DLOG_INFO, LOG_TAG, "logNMEA# NMEA #%d [%s].", i, *nmea );
      else
         dlog_print( DLOG_ERROR, LOG_TAG, "logNMEA# error [%d,%s].", ret, get_error_message( ret ) );
   }

   free( nmea );
}

// EOF.
