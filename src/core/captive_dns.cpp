// A tiny DNS responder (see captive_dns.h), on a non-blocking lwIP UDP socket.
//
// For any standard query with one question:
// - type A or ANY: one answer, the pumpkin's address, with a short TTL
// - anything else (AAAA, HTTPS/SVCB...): "no error, no answers", so phones fall back to A at once
//   rather than waiting for a timeout
// The question is copied back as it came (letter case included, which some resolvers check), and
// anything after it (EDNS's OPT record in the additional section) is ignored. Malformed packets get
// no reply. Every read is checked against the packet's length.

#include "core/captive_dns.h"

#include <cstring>
#include <lwip/sockets.h>

#include "core/logging.h"

namespace
{
    constexpr uint16_t kDnsPort = 53;
    constexpr uint32_t kAnswerTtlSeconds = 30; // short, so phones forget these once on the home WiFi
    constexpr int kQueriesPerServe = 8;        // a phone that has just joined fires off several at once

    constexpr size_t kHeaderBytes = 12;
    constexpr size_t kMaxQueryBytes = 512; // plain DNS over UDP. A longer packet is cut short: the
                                           // question comes first, so that's fine.
    constexpr size_t kMaxNameBytes = 255;
    constexpr uint8_t kMaxLabelBytes = 63; // bigger values are compression pointers: not in a question
    constexpr size_t kAnswerBytes = 16;    // name pointer, type, class, TTL, length, IPv4 address

    constexpr uint16_t kTypeA = 1;
    constexpr uint16_t kTypeAny = 255;
    constexpr uint16_t kClassIn = 1;
    constexpr uint16_t kClassAny = 255;

    // Header flags: first byte
    constexpr uint8_t kFlagReply = 0x80;           // QR
    constexpr uint8_t kOpcodeMask = 0x78;          // 0 = standard query
    constexpr uint8_t kFlagAuthoritative = 0x04;   // AA
    constexpr uint8_t kFlagRecursionWanted = 0x01; // RD, copied from the query
    // Second byte
    constexpr uint8_t kFlagRecursionOffered = 0x80; // RA (the low bits, zero, mean "no error")

    int sock = -1;
    uint8_t address[4];
    uint8_t packet[kMaxQueryBytes + kAnswerBytes]; // the query, turned into the reply in place

    uint16_t read16(const uint8_t *p)
    {
        return static_cast<uint16_t>((p[0] << 8) | p[1]);
    }

    void write16(uint8_t *p, uint16_t value)
    {
        p[0] = static_cast<uint8_t>(value >> 8);
        p[1] = static_cast<uint8_t>(value);
    }

    // Turn the query in `packet` into a reply. Returns the reply's length, or 0 to send nothing.
    size_t buildReply(size_t length)
    {
        if (length < kHeaderBytes + 5) // header, the root name, type and class at the very least
            return 0;
        const uint8_t flags = packet[2];
        if ((flags & kFlagReply) != 0 || (flags & kOpcodeMask) != 0)
            return 0; // a reply, or not a standard query
        if (read16(packet + 4) != 1)
            return 0; // exactly one question

        // Walk the question's name, label by label
        size_t pos = kHeaderBytes;
        size_t nameBytes = 1; // the final zero
        for (;;)
        {
            if (pos >= length)
                return 0;
            const uint8_t label = packet[pos];
            if (label == 0)
            {
                pos++;
                break;
            }
            if (label > kMaxLabelBytes)
                return 0;
            nameBytes += 1 + label;
            if (nameBytes > kMaxNameBytes)
                return 0;
            pos += 1 + label;
        }
        if (pos + 4 > length)
            return 0;
        const uint16_t type = read16(packet + pos);
        const uint16_t dnsClass = read16(packet + pos + 2);
        pos += 4; // the reply ends here, or after the answer

        const bool answer = (type == kTypeA || type == kTypeAny) && (dnsClass == kClassIn || dnsClass == kClassAny);

        packet[2] = kFlagReply | kFlagAuthoritative | (flags & kFlagRecursionWanted);
        packet[3] = kFlagRecursionOffered;
        write16(packet + 6, answer ? 1 : 0); // answers
        write16(packet + 8, 0);              // authority records
        write16(packet + 10, 0);             // additional records (we don't speak EDNS)
        if (answer)
        {
            // pos <= kMaxQueryBytes, and packet has kAnswerBytes to spare beyond that
            uint8_t *a = packet + pos;
            write16(a, 0xC000 | kHeaderBytes); // the name: a pointer to the question's
            write16(a + 2, kTypeA);
            write16(a + 4, kClassIn);
            write16(a + 6, static_cast<uint16_t>(kAnswerTtlSeconds >> 16));
            write16(a + 8, static_cast<uint16_t>(kAnswerTtlSeconds));
            write16(a + 10, static_cast<uint16_t>(sizeof(address)));
            memcpy(a + 12, address, sizeof(address));
            pos += kAnswerBytes;
        }
        return pos;
    }
}

namespace captive_dns
{
    bool start(const IPAddress &ip)
    {
        stop();
        for (int i = 0; i < 4; i++)
            address[i] = ip[i];

        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0)
        {
            LOG("DNS", "Couldn't open a socket");
            return false;
        }
        sockaddr_in local = {};
        local.sin_family = AF_INET;
        local.sin_port = htons(kDnsPort);
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(sock, reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0)
        {
            LOG("DNS", "Couldn't listen on port %u", kDnsPort);
            stop();
            return false;
        }
        return true;
    }

    void serve()
    {
        if (sock < 0)
            return;
        for (int i = 0; i < kQueriesPerServe; i++)
        {
            sockaddr_in from = {};
            socklen_t fromLength = sizeof(from);
            const int received = recvfrom(sock, packet, kMaxQueryBytes, MSG_DONTWAIT,
                                          reinterpret_cast<sockaddr *>(&from), &fromLength);
            if (received <= 0)
                return; // nothing waiting
            const size_t reply = buildReply(static_cast<size_t>(received));
            if (reply > 0)
                sendto(sock, packet, reply, 0, reinterpret_cast<sockaddr *>(&from), fromLength);
        }
    }

    void stop()
    {
        if (sock >= 0)
        {
            close(sock);
            sock = -1;
        }
    }
}
