// lagging packets
#include "iup.h"
#include "common.h"
#define NAME "jitter"
#define JITTER_MIN "0"
#define JITTER_MAX "3000"
#define KEEP_AT_MOST 2000
// send FLUSH_WHEN_FULL packets when buffer is full
#define FLUSH_WHEN_FULL 800
#define JITTER_DEFAULT 50

// don't need a chance
static Ihandle* inboundCheckbox, * outboundCheckbox, * timeInput;

static volatile short JitterEnabled = 0,
lagInbound = 1,
lagOutbound = 1,
lagTime = JITTER_DEFAULT; // default for 50ms

static PacketNode lagHeadNode = { 0 }, lagTailNode = { 0 };
static PacketNode* bufHead = &lagHeadNode, * bufTail = &lagTailNode;
static int bufSize = 0;

static INLINE_FUNCTION short isBufEmpty() {
    short ret = bufHead->next == bufTail;
    if (ret) assert(bufSize == 0);
    return ret;
}

static Ihandle* lagSetupUI() {
    Ihandle* lagControlsBox = IupHbox(
        inboundCheckbox = IupToggle("Inbound", NULL),
        outboundCheckbox = IupToggle("Outbound", NULL),
        IupLabel("  Jitter(ms):"),
        timeInput = IupText(NULL),
        NULL
    );

    IupSetAttribute(timeInput, "VISIBLECOLUMNS", "4");
    IupSetAttribute(timeInput, "VALUE", STR(JITTER_DEFAULT));
    IupSetCallback(timeInput, "VALUECHANGED_CB", uiSyncInteger);
    IupSetAttribute(timeInput, SYNCED_VALUE, (char*)&lagTime);
    IupSetAttribute(timeInput, INTEGER_MAX, JITTER_MAX);
    IupSetAttribute(timeInput, INTEGER_MIN, JITTER_MIN);
    IupSetCallback(inboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(inboundCheckbox, SYNCED_VALUE, (char*)&lagInbound);
    IupSetCallback(outboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(outboundCheckbox, SYNCED_VALUE, (char*)&lagOutbound);

    // enable by default to avoid confusing
    IupSetAttribute(inboundCheckbox, "VALUE", "ON");
    IupSetAttribute(outboundCheckbox, "VALUE", "ON");

    if (parameterized) {
        setFromParameter(inboundCheckbox, "VALUE", NAME"-inbound");
        setFromParameter(outboundCheckbox, "VALUE", NAME"-outbound");
        setFromParameter(timeInput, "VALUE", NAME"-time");
    }

    return lagControlsBox;
}

static void lagStartUp() {
    if (bufHead->next == NULL && bufTail->next == NULL) {
        bufHead->next = bufTail;
        bufTail->prev = bufHead;
        bufSize = 0;
    }
    else {
        assert(isBufEmpty());
    }
    startTimePeriod();
}

static void lagCloseDown(PacketNode* head, PacketNode* tail) {
    PacketNode* oldLast = tail->prev;
    UNREFERENCED_PARAMETER(head);
    // flush all buffered packets
    LOG("Closing down lag, flushing %d packets", bufSize);
    while (!isBufEmpty()) {
        insertAfter(popNode(bufTail->prev), oldLast);
        --bufSize;
    }
    endTimePeriod();
}

static short jitterProcess(PacketNode* head, PacketNode* tail) {
    DWORD currentTime = timeGetTime();
    PacketNode* pac = tail->prev;
    // pick up all packets and fill in the current time
    while (bufSize < KEEP_AT_MOST && pac != head) {
        if (checkDirection(pac->addr.Direction, lagInbound, lagOutbound)) {
            int jitter = lagTime ? (rand() % lagTime) : 0;
            insertAfter(popNode(pac), bufHead)->timestamp = timeGetTime() + jitter;
            LOG("Jiiter %p %d", pac, jitter);
            ++bufSize;
            pac = tail->prev;
        }
        else {
            pac = pac->prev;
        }
    }

    pac = bufTail->prev;
    while (!isBufEmpty() && pac != bufHead) {
        if (currentTime > pac->timestamp) {
            PacketNode* temp = pac->prev;
            insertAfter(popNode(pac), head); // sending queue is already empty by now
            LOG("Send lagged packets. %p", pac);
            pac = temp;
            --bufSize;
        }
        else {
            //LOG("Sent some lagged packets, still have %d in buf  %p", bufSize, pac);
            pac = pac->prev;
            continue;
        }
    }

    // if buffer is full just flush things out
    if (bufSize >= KEEP_AT_MOST) {
        int flushCnt = FLUSH_WHEN_FULL;
        while (flushCnt-- > 0) {
            insertAfter(popNode(bufTail->prev), head);
            --bufSize;
        }
    }

    return bufSize > 0;
}

Module jitterModule = {
    "Jitter",
    NAME,
    (short*)&JitterEnabled,
    lagSetupUI,
    lagStartUp,
    lagCloseDown,
    jitterProcess,
    // runtime fields
    0, 0, NULL
};