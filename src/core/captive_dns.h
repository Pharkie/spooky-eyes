#pragma once
// Captive-portal DNS for the setup WiFi: every name a phone looks up points at the pumpkin, so the
// phone's "sign in to network" page opens the setup page. Replaces Arduino's DNSServer, which
// answered queries carrying EDNS (most modern phones) with a broken reply and didn't check lengths.
// Only the network task uses it.

#include <IPAddress.h>

namespace captive_dns
{
    // Start answering on UDP port 53 with `ip`. False if the socket couldn't be opened.
    bool start(const IPAddress &ip);

    // Answer the queries waiting (never blocks): call every 10-20 ms
    void serve();

    void stop();
}
