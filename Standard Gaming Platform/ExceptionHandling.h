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


#ifdef __cplusplus
}
#endif

#endif