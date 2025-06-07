//
// Created by overflow on 5/30/2025.
//

#ifndef GLPS_MQTT_CLIENT_H
#define GLPS_MQTT_CLIENT_H

#include <stdint.h>

#ifdef GLPS_USE_WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

typedef struct {
    const char* addr;
    uint16_t port;
    bool is_connected;
} glps_mqtt_connection;

glps_mqtt_connection* glps_mqtt_connect(const char* addr, uint16_t port);
void glps_mqtt_disconnect(glps_mqtt_connection* connection_ptr);

#endif //GLPS_MQTT_CLIENT_H
