/*!
** \file    andygmpub-gps.c
** \date    2014/12/17 08:00
** \brief   andygmpub GPS service, implementation.
** \author  A.Godinho (Woody)
**/

#include <tizen.h>
#include <service_app.h>

#include "andygmpub-gps.h"
#include "location_manager.h"

bool
onServiceCreate( void *data )
{
   return locationInitialize();
}

void
onServiceTerminate( void *data )
{
   locationFinalize();
}

void
onServiceControl( app_control_h app_control, void *data )
{
}

static void
onLowBattery( app_event_info_h event_info, void *user_data )
{
   locationStop();
}

static void
onLowMemory( app_event_info_h event_info, void *user_data )
{
}

static void
onLangChange( app_event_info_h event_info, void *user_data )
{
}

static void
onRegionChange( app_event_info_h event_info, void *user_data )
{
}

int
main( int argc, char* argv[ ] )
{
   char ad[ 50 ] = { 0, };
   service_app_lifecycle_callback_s event_callback;
   app_event_handler_h handlers[ 5 ] = { NULL, };

   event_callback.create = onServiceCreate;
   event_callback.terminate = onServiceTerminate;
   event_callback.app_control = onServiceControl;

   service_app_add_event_handler(
      &handlers[ APP_EVENT_LOW_BATTERY ],
      APP_EVENT_LOW_BATTERY,
      onLowBattery,
      &ad
   );
   service_app_add_event_handler(
      &handlers[ APP_EVENT_LOW_MEMORY ],
      APP_EVENT_LOW_MEMORY,
      onLowMemory,
      &ad
   );
   service_app_add_event_handler(
      &handlers[ APP_EVENT_LANGUAGE_CHANGED ],
      APP_EVENT_LANGUAGE_CHANGED,
      onLangChange,
      &ad
   );
   service_app_add_event_handler(
      &handlers[ APP_EVENT_REGION_FORMAT_CHANGED ],
      APP_EVENT_REGION_FORMAT_CHANGED,
      onRegionChange,
      &ad
   );

   return service_app_main( argc, argv, &event_callback, ad );
}
