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
static void Ping(void);

static HANDLE hIcmpFile = INVALID_HANDLE_VALUE;
static ULONG Timeout = 4000;
static IPAddr TargetAddr = INADDR_NONE;
static int Family = AF_INET;
static USHORT RequestSize = 32;
static ULONG PingCount = 4;

int
wmain(int argc, WCHAR *argv[])
{
    WSADATA wsaData;

    if (!ParseCmdLine(argc, argv))
    {
        return 1;
    }

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        wprintf(L"WSAStartup failed\n");

        return 1;
    }

    if (!ResolveTarget(argv[1]))
    {
        WSACleanup();

        return 1;
    }

    hIcmpFile = IcmpCreateFile();
    if (hIcmpFile == INVALID_HANDLE_VALUE)
    {
        wprintf(L"IcmpCreateFile failed: %lu\n", GetLastError());
        WSACleanup();

        return 1;
    }

    wprintf(L"\nPinging %s with %u bytes of data:\n", argv[1], RequestSize);
    for (ULONG i = 0; i < PingCount; i++)
    {
        Ping();

        if (i < PingCount - 1)
            Sleep(1000);
    }

    IcmpCloseHandle(hIcmpFile);
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
        wprintf(L"Usage: ping target");

        return false;
    }

    return true;
}

static
bool
ResolveTarget(PCWSTR target)
{
    ADDRINFOW hints;
    ADDRINFOW *results;
    int Status;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = Family;

    Status = GetAddrInfoW(target, NULL, &hints, &results);
    if (Status != 0)
    {
        printf("GetAddrInfoW failed: %d\n", Status);

        return false;
    }

    TargetAddr = ((PSOCKADDR_IN)results->ai_addr)->sin_addr.s_addr;
    if (TargetAddr == INADDR_NONE)
    {
        FreeAddrInfoW(results);

        return false;
    }

    FreeAddrInfoW(results);

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

    ZeroMemory(ReplyBuffer, ReplySize);

    ReplySize = sizeof(ICMP_ECHO_REPLY) + RequestSize + 8;
    ReplyBuffer = malloc(ReplySize);
    if (ReplyBuffer == NULL)
    {
        wprintf(L"malloc failed\n");
        free(SendBuffer);

        exit(1);
    }

    ZeroMemory(ReplyBuffer, ReplySize);

    Status = IcmpSendEcho2(
        hIcmpFile, NULL, NULL, NULL,
        TargetAddr, SendBuffer, RequestSize,
        NULL, ReplyBuffer, ReplySize, Timeout);

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
        PICMP_ECHO_REPLY pEchoReply;
        IN_ADDR ReplyAddress;
        WCHAR Dest[16];

        pEchoReply = (PICMP_ECHO_REPLY)ReplyBuffer;
        ReplyAddress.s_addr = pEchoReply->Address;

        if (InetNtopW(Family, &ReplyAddress, Dest, _countof(Dest)) == NULL)
        {
            wprintf(L"InetNtopW failed\n");
            free(ReplyBuffer);

            exit(1);
        }

        wprintf(L"Reply from %s:", Dest);
        wprintf(L" bytes=%u", pEchoReply->DataSize);
        wprintf(L" time=%lums", pEchoReply->RoundTripTime);
        wprintf(L" TTL=%u\n", pEchoReply->Options.Ttl);
    }

    free(ReplyBuffer);
}
