#define WIN32_LEAN_AND_MEAN

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <ws2tcpip.h>

static bool ParseCmdLine(int argc, PCWSTR argv[]);
static bool ResolveTarget(PCWSTR target);
static void Usage(void);
static void Ping(void);

static HANDLE hIcmpFile = INVALID_HANDLE_VALUE;
static ULONG Timeout = 4000;
static int Family = AF_UNSPEC;
static ULONG RequestSize = 32;
static ULONG PingCount = 4;
static PADDRINFOW TargetAddrInfo = NULL;
static PCWSTR TargetName = NULL;
static WCHAR TargetAddrStr[46];

int
wmain(int argc, WCHAR *argv[])
{
    WSADATA wsaData;
    DWORD StrLen = 46;

    if (!ParseCmdLine(argc, argv))
    {
        return 1;
    }

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        wprintf(L"WSAStartup failed\n");

        return 1;
    }

    if (!ResolveTarget(TargetName))
    {
        WSACleanup();

        return 1;
    }

    if (WSAAddressToStringW(TargetAddrInfo->ai_addr, (DWORD)TargetAddrInfo->ai_addrlen, NULL, TargetAddrStr, &StrLen) != 0)
    {
        wprintf(L"WSAAddressToStringW failed: %d\n", WSAGetLastError());
        FreeAddrInfoW(TargetAddrInfo);
        WSACleanup();

        return 1;
    }

    if (Family == AF_INET6)
    {
        hIcmpFile = Icmp6CreateFile();
    }
    else
    {
        hIcmpFile = IcmpCreateFile();
    }


    if (hIcmpFile == INVALID_HANDLE_VALUE)
    {
        wprintf(L"IcmpCreateFile failed: %lu\n", GetLastError());
        WSACleanup();

        return 1;
    }

    wprintf(L"\nPinging %s with %u bytes of data:\n", TargetName, RequestSize);
    for (ULONG i = 0; i < PingCount; i++)
    {
        Ping();

        if (i < PingCount - 1)
            Sleep(1000);
    }

    IcmpCloseHandle(hIcmpFile);
    FreeAddrInfoW(TargetAddrInfo);
    WSACleanup();

    return 0;
}

static
bool
ParseCmdLine(int argc, PCWSTR argv[])
{
    UNREFERENCED_PARAMETER(argv);

    if (argc < 2)
    {
        Usage();

        return false;
    }

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == L'-' || argv[i][0] == L'/')
        {
            switch (argv[i][1])
            {
            case L'n':
                if (i + 1 < argc)
                {
                    PingCount = wcstoul(argv[++i], NULL, 0);

                    if (PingCount == 0)
                    {
                        wprintf(L"Bad value for option %s.\n", argv[i - 1]);

                        return false;
                    }
                }
                else
                {
                    wprintf(L"Value must be supplied for option %s.\n", argv[i]);

                    return false;
                }

                break;

            case L'l':
                if (i + 1 < argc)
                {
                    RequestSize = wcstoul(argv[++i], NULL, 0);

                    if (RequestSize >= 65500)
                    {
                        wprintf(L"Bad value for option %s.\n", argv[i - 1]);

                        return false;
                    }
                }
                else
                {
                    wprintf(L"Value must be supplied for option %s.\n", argv[i]);

                    return false;
                }

                break;

            case L'v':
                if (Family == AF_INET6)
                {
                    wprintf(L"The option %s is only supported for %s.\n", argv[i], L"IPv4");

                    return false;
                }

                Family = AF_INET;

                if (i + 1 < argc)
                {
                    /* This option has been deprecated. Don't do anything. */
                    ++i;
                }
                else
                {
                    wprintf(L"Value must be supplied for option %s.\n", argv[i]);

                    return false;
                }

                break;

            case L'w':
                if (i + 1 < argc)
                {
                    Timeout = wcstoul(argv[++i], NULL, 0);

                    if (Timeout < 1000)
                    {
                        Timeout = 1000;
                    }
                }
                else
                {
                    wprintf(L"Value must be supplied for option %s.\n", argv[i]);

                    return false;
                }

                break;

            case L'R':
                if (Family == AF_INET)
                {
                    wprintf(L"The option %s is only supported for %s.\n", argv[i], L"IPv6");

                    return false;
                }

                Family = AF_INET6;

                /* This option has been deprecated. Don't do anything. */
                break;

            case L'4':
                if (Family == AF_INET6)
                {
                    wprintf(L"The option %s is only supported for %s.\n", argv[i], L"IPv4");

                    return false;
                }

                Family = AF_INET;
                break;

            case L'6':
                if (Family == AF_INET)
                {
                    wprintf(L"The option %s is only supported for %s.\n", argv[i], L"IPv6");

                    return false;
                }

                Family = AF_INET6;
                break;

            default:
                wprintf(L"Unrecognized parameter %s\n", argv[i]);
                return false;
            }
        }
        else
        {
            TargetName = argv[i];
        }
    }

    if (TargetName == NULL)
    {
        wprintf(L"IP address must be specified.\n");

        return false;
    }

    return true;
}

static
bool
ResolveTarget(PCWSTR target)
{
    ADDRINFOW hints;
    int Status;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = Family;

    Status = GetAddrInfoW(target, NULL, &hints, &TargetAddrInfo);
    if (Status != 0)
    {
        printf("GetAddrInfoW failed: %d\n", Status);

        return false;
    }

    Family = TargetAddrInfo->ai_family;

    return true;
}

static
void
Ping(void)
{
    PVOID ReplyBuffer = NULL;
    PVOID SendBuffer = NULL;
    DWORD ReplySize = 0;
    DWORD Status;

    SendBuffer = malloc(RequestSize);
    if (SendBuffer == NULL)
    {
        wprintf(L"malloc failed\n");

        exit(1);
    }

    ZeroMemory(SendBuffer, RequestSize);

    if (Family == AF_INET6)
    {
        ReplySize = sizeof(ICMPV6_ECHO_REPLY) + RequestSize + 8;
    }
    else
    {
        ReplySize = sizeof(ICMP_ECHO_REPLY) + RequestSize + 8;
    }

    ReplyBuffer = malloc(ReplySize);
    if (ReplyBuffer == NULL)
    {
        wprintf(L"malloc failed\n");
        free(SendBuffer);

        exit(1);
    }

    ZeroMemory(ReplyBuffer, ReplySize);

    if (Family == AF_INET6)
    {
        SOCKADDR_IN6 Source;

        ZeroMemory(&Source, sizeof(Source));
        Source.sin6_addr = in6addr_any;
        Source.sin6_family = AF_INET6;

        Status = Icmp6SendEcho2(
            hIcmpFile, NULL, NULL, NULL,
            &Source,
            (PSOCKADDR_IN6)TargetAddrInfo->ai_addr,
            SendBuffer, (USHORT)RequestSize, NULL,
            ReplyBuffer, ReplySize, Timeout);

    }
    else
    {
        Status = IcmpSendEcho2Ex(
            hIcmpFile, NULL, NULL, NULL,
            INADDR_ANY,
            ((PSOCKADDR_IN)TargetAddrInfo->ai_addr)->sin_addr.s_addr,
            SendBuffer, (USHORT)RequestSize, NULL,
            ReplyBuffer, ReplySize, Timeout);

    }

    free(SendBuffer);

    if (Status == 0)
    {
        Status = GetLastError();
        switch (Status)
        {
        case IP_DEST_HOST_UNREACHABLE:
            wprintf(L"Destination host unreachable.\n");
            break;

        case IP_DEST_NET_UNREACHABLE:
            wprintf(L"Destination net unreachable.\n");
            break;

        case IP_REQ_TIMED_OUT:
            wprintf(L"Request timed out.\n");
            break;

        default:
            wprintf(L"PING: transmit failed. General failure. (Error %lu)\n", Status);
            break;
        }
    }
    else
    {
        if (Family == AF_INET6)
        {
            PICMPV6_ECHO_REPLY pEchoReply;

            pEchoReply = (PICMPV6_ECHO_REPLY)ReplyBuffer;

            wprintf(L"Reply from %s:", TargetAddrStr);

            if (pEchoReply->RoundTripTime == 0)
            {
                wprintf(L" time<1ms\n");
            }
            else
            {
                wprintf(L" time=%lums\n", pEchoReply->RoundTripTime);
            }

        }
        else
        {
            PICMP_ECHO_REPLY pEchoReply;

            pEchoReply = (PICMP_ECHO_REPLY)ReplyBuffer;

            wprintf(L"Reply from %s:", TargetAddrStr);
            wprintf(L" bytes=%u", pEchoReply->DataSize);

            if (pEchoReply->RoundTripTime == 0)
            {
                wprintf(L" time<1ms");
            }
            else
            {
                wprintf(L" time=%lums", pEchoReply->RoundTripTime);
            }

            wprintf(L" TTL=%u\n", pEchoReply->Options.Ttl);
        }
    }

    free(ReplyBuffer);
}

static
void
Usage(void)
{
    wprintf(L"\n\
Usage: ping [-n count] [-l size] [-4] [-6] [-w timeout] target\n\
\n\
Options:\n\
    -n count    Number of echo requests to send.\n\
    -l size     Send buffer size.\n\
    -w timeout  Timeout in milliseconds to wait for each reply.\n\
    -4          Force using IPv4.\n\
    -6          Force using IPv6.\n\
\n");
}
