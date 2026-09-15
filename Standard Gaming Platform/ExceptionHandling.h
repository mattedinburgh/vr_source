#ifndef _EXCEPTION_HANDLING__H_
#define _EXCEPTION_HANDLING__H_

#include <Windows.h>

//uncomment this line if you want Exceptions to be handled
#ifdef JA2

#ifndef _DEBUG
	#define ENABLE_EXCEPTION_HANDLING
#endif

#else
	//Wizardry 
//#define ENABLE_EXCEPTION_HANDLING
#endif


#ifdef __cplusplus
extern "C" {
#endif



INT32 RecordExceptionInfo( EXCEPTION_POINTERS *pExceptInfo );

// Vengeance flight recorder / crash black box.
// BlackBoxEvent() is for durable, low-frequency milestones.
// BlackBoxCheckpoint() is memory-only and safe to call from hot diagnostic paths.
void BlackBoxInitialize( void );
void BlackBoxShutdown( void );
void BlackBoxEvent( const char *category, const char *format, ... );
void BlackBoxCheckpoint( const char *subsystem, const char *format, ... );
// Call once per main-loop iteration. This is intentionally cheap: it updates
// lock-free heartbeat state every frame and only emits health diagnostics at
// throttled intervals.
enum
{
	BLACKBOX_PHASE_UNKNOWN = 0,
	BLACKBOX_PHASE_FRAME_BEGIN,
	BLACKBOX_PHASE_INPUT,
	BLACKBOX_PHASE_SCREEN_HANDLER,
	BLACKBOX_PHASE_RENDER,
	BLACKBOX_PHASE_CLOCK,
	BLACKBOX_PHASE_NETWORK,
	BLACKBOX_PHASE_FRAME_END
};

void BlackBoxHeartbeat( DWORD currentScreen );
void BlackBoxFramePhase( DWORD phase );

// Structured v5 telemetry. These are intentionally fixed-size/no-allocation
// APIs so they remain predictable in an old 32-bit engine.
void BlackBoxContext( const char *key, const char *format, ... );
LONG BlackBoxCounterAdd( const char *name, LONG delta );
DWORD BlackBoxOperationBegin( const char *subsystem, const char *name );
void BlackBoxOperationEnd( DWORD token, const char *result );


#ifdef __cplusplus
}
#endif

#endif