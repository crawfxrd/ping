#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#define SIZEOF_ICMP_ERROR 8
#define SIZEOF_IO_STATUS_BLOCK 8

static BOOL ParseCmdLine(int argc, PWSTR argv[]);
static BOOL ResolveTarget(PCWSTR target);
static void Usage(void);
static void Ping(void);
static void PrintStats(void);
static BOOL WINAPI ConsoleCtrlHandler(DWORD ControlType);

static HANDLE hIcmpFile = INVALID_HANDLE_VALUE;
static ULONG Timeout = 4000;
static int Family = AF_UNSPEC;
static ULONG RequestSize = 32;
static ULONG PingCount = 4;
static BOOL PingForever = FALSE;
static PADDRINFOW Target = NULL;
static PCWSTR TargetName = NULL;
static WCHAR Address[46];
static WCHAR CanonName[NI_MAXHOST];
static BOOL ResolveAddress = FALSE;

static ULONG RTTMax = 0;
static ULONG RTTMin = 0;
static ULONG RTTTotal = 0;
static ULONG EchosSent = 0;
static ULONG EchosReceived = 0;
static ULONG EchosSuccessful = 0;

static IP_OPTION_INFORMATION IpOptions;

int
wmain(int argc, WCHAR *argv[])
{
    WSADATA wsaData;
    ULONG i;
    DWORD StrLen = 46;

    IpOptions.Ttl = 128;

    if (!ParseCmdLine(argc, argv))
    {
        return 1;
    }

    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE))
    {
        wprintf(L"Failed to set control handler.\n");

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

    if (WSAAddressToStringW(Target->ai_addr, (DWORD)Target->ai_addrlen, NULL, Address, &StrLen) != 0)
    {
        wprintf(L"WSAAddressToStringW failed: %d\n", WSAGetLastError());
        FreeAddrInfoW(Target);
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
        FreeAddrInfoW(Target);
        WSACleanup();

        return 1;
    }

    if (*CanonName)
    {
        wprintf(L"\nPinging %s [%s] with %u bytes of data:\n", CanonName, Address, RequestSize);
    }
    else
    {
        wprintf(L"\nPinging %s with %u bytes of data:\n", Address, RequestSize);
    }

    Ping();
    i = 1;

    while (i < PingCount)
    {
        Sleep(1000);
        Ping();

        if (!PingForever)
            ++i;
    }

    PrintStats();

    IcmpCloseHandle(hIcmpFile);
    FreeAddrInfoW(Target);
    WSACleanup();

    return 0;
}

static
void
Usage(void)
{
    wprintf(L"\n\
Usage: ping [-t] [-a] [-n count] [-l size] [-f] [-i TTL] [-v TOS]\n\
            [-w timeout] [-4] [-6] target\n\
\n\
Options:\n\
    -t          Ping the specified host until stopped.\n\
                To see statistics and continue - type Control-Break;\n\
                To stop - type Control-C.\n\
    -a          Resolve addresses to hostnames.\n\
    -n count    Number of echo requests to send.\n\
    -l size     Send buffer size.\n\
    -f          Set Don't Fragment flag in packet (IPv4-only).\n\
    -i TTL      Time To Live.\n\
    -v TOS      Type Of Service (IPv4-only. This setting has been deprecated\n\
                and has no effect on the type of service field in the IP\n\
                Header).\n\
    -w timeout  Timeout in milliseconds to wait for each reply.\n\
    -4          Force using IPv4.\n\
    -6          Force using IPv6.\n\
\n");
}

static
BOOL
ParseCmdLine(int argc, PWSTR argv[])
{
    int i;

    if (argc < 2)
    {
        Usage();

        return FALSE;
    }

    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == L'-' || argv[i][0] == L'/')
        {
            switch (argv[i][1])
            {
            case L't':
                PingForever = TRUE;
                break;

            case L'a':
                ResolveAddress = TRUE;
                break;

            case L'n':
                if (i + 1 < argc)
                {
                    PingForever = FALSE;
                    PingCount = wcstoul(argv[++i], NULL, 0);

                    if (PingCount == 0)
                    {
                        wprintf(L"Bad value for option %s.\n", argv[i - 1]);

                        return FALSE;
                    }
                }
                else
                {
                    wprintf(L"Value must be supplied for option %s.\n", argv[i]);

                    return FALSE;
                }

                break;

            case L'l':
                if (i + 1 < argc)
                {
                    RequestSize = wcstoul(argv[++i], NULL, 0);

                    if (RequestSize > 65500)
                    {
                        wprintf(L"Bad value for option %s.\n", argv[i - 1]);

                        return FALSE;
                    }
                }
                else
                {
                    wprintf(L"Value must be supplied for option %s.\n", argv[i]);

                    return FALSE;
                }

                break;

            case L'f':
                if (Family == AF_INET6)
                {
                    wprintf(L"The option %s is only supported for %s.\n", argv[i], L"IPv4");

                    return FALSE;
                }

                Family = AF_INET;
                IpOptions.Flags |= IP_FLAG_DF;
                break;

            case L'i':
                if (i + 1 < argc)
                {
                    ULONG Ttl = wcstoul(argv[++i], NULL, 0);

                    if ((Ttl == 0) || (Ttl > 255))
                    {
                        wprintf(L"Bad value for option %s.\n", argv[i - 1]);

                        return FALSE;
                    }

                    IpOptions.Ttl = (UCHAR)Ttl;
                }
                else
                {
                    wprintf(L"Value must be supplied for option %s.\n", argv[i]);

                    return FALSE;
                }

                break;

            case L'v':
                if (Family == AF_INET6)
                {
                    wprintf(L"The option %s is only supported for %s.\n", argv[i], L"IPv4");

                    return FALSE;
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

                    return FALSE;
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

                    return FALSE;
                }

                break;

            case L'R':
                if (Family == AF_INET)
                {
                    wprintf(L"The option %s is only supported for %s.\n", argv[i], L"IPv6");

                    return FALSE;
                }

                Family = AF_INET6;

                /* This option has been deprecated. Don't do anything. */
                break;

            case L'4':
                if (Family == AF_INET6)
                {
                    wprintf(L"The option %s is only supported for %s.\n", argv[i], L"IPv4");

                    return FALSE;
                }

                Family = AF_INET;
                break;

            case L'6':
                if (Family == AF_INET)
                {
                    wprintf(L"The option %s is only supported for %s.\n", argv[i], L"IPv6");

                    return FALSE;
                }

                Family = AF_INET6;
                break;

            case L'?':
                Usage();

                return FALSE;

            default:
                wprintf(L"Bad option %s.\n", argv[i]);
                Usage();

                return FALSE;
            }
        }
        else
        {
            if (TargetName != NULL)
            {
                wprintf(L"Bad parameter %s.\n", argv[i]);

                return FALSE;
            }

            TargetName = argv[i];
        }
    }

    if (TargetName == NULL)
    {
        wprintf(L"IP address must be specified.\n");

        return FALSE;
    }

    return TRUE;
}

static
BOOL
ResolveTarget(PCWSTR target)
{
    ADDRINFOW hints;
    int Status;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = Family;
    hints.ai_flags = AI_NUMERICHOST;

    Status = GetAddrInfoW(target, NULL, &hints, &Target);
    if (Status != 0)
    {
        hints.ai_flags = AI_CANONNAME;

        Status = GetAddrInfoW(target, NULL, &hints, &Target);
        if (Status != 0)
        {
            wprintf(L"GetAddrInfoW failed: %d\n", Status);

            return FALSE;
        }

        wcsncpy(CanonName, Target->ai_canonname, wcslen(Target->ai_canonname));
    }
    else if (ResolveAddress)
    {
        Status = GetNameInfoW(
            Target->ai_addr, Target->ai_addrlen,
            CanonName, _countof(CanonName),
            NULL, 0,
            NI_NAMEREQD);

        if (Status != 0)
        {
            wprintf(L"GetNameInfoW failed: %d\n", Status);

            return FALSE;
        }
    }

    Family = Target->ai_family;

    return TRUE;
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
        ReplySize += sizeof(ICMPV6_ECHO_REPLY);
    }
    else
    {
        ReplySize += sizeof(ICMP_ECHO_REPLY);
    }

    ReplySize += RequestSize + SIZEOF_ICMP_ERROR + SIZEOF_IO_STATUS_BLOCK;

    ReplyBuffer = malloc(ReplySize);
    if (ReplyBuffer == NULL)
    {
        wprintf(L"malloc failed\n");
        free(SendBuffer);

        exit(1);
    }

    ZeroMemory(ReplyBuffer, ReplySize);

    ++EchosSent;

    if (Family == AF_INET6)
    {
        struct sockaddr_in6 Source;

        ZeroMemory(&Source, sizeof(Source));
        Source.sin6_addr = in6addr_any;
        Source.sin6_family = AF_INET6;

        Status = Icmp6SendEcho2(
            hIcmpFile, NULL, NULL, NULL,
            &Source,
            (struct sockaddr_in6 *)Target->ai_addr,
            SendBuffer, (USHORT)RequestSize, &IpOptions,
            ReplyBuffer, ReplySize, Timeout);
    }
    else
    {
        Status = IcmpSendEcho2(
            hIcmpFile, NULL, NULL, NULL,
            ((PSOCKADDR_IN)Target->ai_addr)->sin_addr.s_addr,
            SendBuffer, (USHORT)RequestSize, &IpOptions,
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
        ++EchosReceived;

        wprintf(L"Reply from %s: ", Address);

        if (Family == AF_INET6)
        {
            PICMPV6_ECHO_REPLY pEchoReply;

            pEchoReply = (PICMPV6_ECHO_REPLY)ReplyBuffer;

            switch (pEchoReply->Status)
            {
            case IP_SUCCESS:
                ++EchosSuccessful;

                if (pEchoReply->RoundTripTime == 0)
                {
                    wprintf(L"time<1ms\n");
                }
                else
                {
                    wprintf(L"time=%lums\n", pEchoReply->RoundTripTime);
                }

                if (pEchoReply->RoundTripTime < RTTMin || RTTMin == 0)
                {
                    RTTMin = pEchoReply->RoundTripTime;
                }

                if (pEchoReply->RoundTripTime > RTTMax || RTTMax == 0)
                {
                    RTTMax = pEchoReply->RoundTripTime;
                }

                RTTTotal += pEchoReply->RoundTripTime;
                break;

            case IP_TTL_EXPIRED_TRANSIT:
                wprintf(L"TTL expired in transit.\n");
                break;

            default:
                wprintf(L"Echo reply returned %lu.\n", pEchoReply->Status);
                break;
            }
        }
        else
        {
            PICMP_ECHO_REPLY pEchoReply;

            pEchoReply = (PICMP_ECHO_REPLY)ReplyBuffer;

            switch (pEchoReply->Status)
            {
            case IP_SUCCESS:
                ++EchosSuccessful;

                wprintf(L"bytes=%u ", pEchoReply->DataSize);

                if (pEchoReply->RoundTripTime == 0)
                {
                    wprintf(L"time<1ms ");
                }
                else
                {
                    wprintf(L"time=%lums ", pEchoReply->RoundTripTime);
                }

                wprintf(L"TTL=%u\n", pEchoReply->Options.Ttl);

                if (pEchoReply->RoundTripTime < RTTMin || RTTMin == 0)
                {
                    RTTMin = pEchoReply->RoundTripTime;
                }

                if (pEchoReply->RoundTripTime > RTTMax || RTTMax == 0)
                {
                    RTTMax = pEchoReply->RoundTripTime;
                }

                RTTTotal += pEchoReply->RoundTripTime;
                break;

            case IP_TTL_EXPIRED_TRANSIT:
                wprintf(L"TTL expired in transit.\n");
                break;

            default:
                wprintf(L"Echo reply returned %lu.\n", pEchoReply->Status);
                break;
            }
        }
    }

    free(ReplyBuffer);
}

static
void
PrintStats(void)
{
    ULONG EchosLost = EchosSent - EchosReceived;
    int PercentLost = (int)((EchosLost / (double)EchosSent) * 100.0);

    wprintf(L"\nPing statistics for %s:\n", Address);
    wprintf(L"    Packets: Sent = %lu, Received = %lu, Lost = %lu (%d%% loss),\n",
        EchosSent, EchosReceived, EchosLost, PercentLost);

    if (EchosSuccessful > 0)
    {
        ULONG RTTAverage = RTTTotal / EchosSuccessful;

        wprintf(L"Approximate round trip times in milli-seconds:\n");
        wprintf(L"    Minimum = %lums, Maximum = %lums, Average = %lums\n",
            RTTMin, RTTMax, RTTAverage);
    }
}

static
BOOL
WINAPI
ConsoleCtrlHandler(DWORD ControlType)
{
    switch (ControlType)
    {
    case CTRL_C_EVENT:
        PrintStats();
        wprintf(L"Control-C\n");
        return FALSE;

    case CTRL_BREAK_EVENT:
        PrintStats();
        wprintf(L"Control-Break\n");
        return TRUE;

    case CTRL_CLOSE_EVENT:
        PrintStats();
        return FALSE;

    default:
        return FALSE;
    }
}
