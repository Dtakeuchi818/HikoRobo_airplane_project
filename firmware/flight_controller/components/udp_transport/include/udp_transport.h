#pragma once

// Blocking UDP server task. Bind to port 5005 after Wi-Fi gets an IP address.
void udp_transport_run(void *arg);
