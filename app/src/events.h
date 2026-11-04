#ifndef SC_EVENTS_H
#define SC_EVENTS_H

#include "common.h"

#include <stdbool.h>
#include <stdint.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>

enum {
    SC_EVENT_DEVICE_DISCONNECTED = SDL_EVENT_USER,
    SC_EVENT_SERVER_CONNECTION_FAILED,
    SC_EVENT_SERVER_CONNECTED,
    SC_EVENT_CONTROLLER_ERROR,
};

bool
sc_push_event_impl(uint32_t type, void *ptr, const char *name);

#define sc_push_event(TYPE) sc_push_event_impl(TYPE, NULL, # TYPE)

#endif
