#ifndef EVENTEDITORDEFINES

#define EVENTEDITORDEFINES

// Gadgets -------------------------------------------
#define EVENTGADGETID_START GADGET_ID_START+50

//#define EVENT_FILTER_ID EVENTGADGETID_START+5
#define EVENT_CREATE_ID EVENTGADGETID_START+6

#define CREATE_NOTEON 1
#define CREATE_POLYPRESSURE 2
#define CREATE_CONTROLCHANGE 4
#define CREATE_PROGRAMCHANGE 8
#define CREATE_CHANNELPRESSURE 16
#define CREATE_PITCHBEND 32
#define CREATE_SYSEX 64

// Edit Position Defines

#define NOEVENTEDIT 0

// Measure Edit
#define EVENTEDIT_1000 10
#define EVENTEDIT_0100 11
#define EVENTEDIT_0010 12
#define EVENTEDIT_0001 13

// Time Edit H-min-sec-frames-s-frames
#define EVENTEDIT_HOUR 20
#define EVENTEDIT_MIN 21
#define EVENTEDIT_SEC 22
#define EVENTEDIT_FRAME 23
#define EVENTEDIT_QFRAME 24

#endif
