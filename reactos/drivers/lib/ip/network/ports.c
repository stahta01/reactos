/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        tcpip/ports.c
 * PURPOSE:     Port allocation
 * PROGRAMMERS: arty (ayerkes@speakeasy.net)
 * REVISIONS:
 *   arty 20041114 Created
 */

#include "precomp.h"

VOID PortsStartup( PPORT_SET PortSet, 
		   UINT StartingPort,
		   UINT PortsToManage ) {
    PortSet->StartingPort = StartingPort;
    PortSet->PortsToOversee = PortsToManage;
    PortSet->ProtoBitBuffer = 
	PoolAllocateBuffer( (PortSet->PortsToOversee + 7) / 8 );
    RtlInitializeBitMap( &PortSet->ProtoBitmap, 
			 PortSet->ProtoBitBuffer,
			 PortSet->PortsToOversee );
    RtlClearBits( &PortSet->ProtoBitmap, 
                  PortSet->StartingPort, PortsToManage );
    ExInitializeFastMutex( &PortSet->Mutex );
}

VOID PortsShutdown( PPORT_SET PortSet ) {
    PoolFreeBuffer( PortSet->ProtoBitBuffer );
}

VOID DeallocatePort( PPORT_SET PortSet, ULONG Port ) {
    Port = htons(Port);
    RtlClearBits( &PortSet->ProtoBitmap, 
		  PortSet->StartingPort + Port, 1 );
}

BOOLEAN AllocatePort( PPORT_SET PortSet, ULONG Port ) {
    BOOLEAN Clear;

    Port = htons(Port);
    Port -= PortSet->StartingPort;

    ExAcquireFastMutex( &PortSet->Mutex );
    Clear = RtlAreBitsClear( &PortSet->ProtoBitmap, Port, 1 );
    if( Clear ) RtlSetBits( &PortSet->ProtoBitmap, Port, 1 );
    ExReleaseFastMutex( &PortSet->Mutex );

    return Clear;
}

ULONG AllocateAnyPort( PPORT_SET PortSet ) {
    ULONG AllocatedPort;

    ExAcquireFastMutex( &PortSet->Mutex );
    AllocatedPort = RtlFindClearBits( &PortSet->ProtoBitmap, 1, 0 );
    if( AllocatedPort != (ULONG)-1 ) {
	RtlSetBit( &PortSet->ProtoBitmap, AllocatedPort );
	AllocatedPort += PortSet->StartingPort;
    }
    ExReleaseFastMutex( &PortSet->Mutex );

    AllocatedPort = htons(AllocatedPort);

    return AllocatedPort;
}

ULONG AllocatePortFromRange( PPORT_SET PortSet, ULONG Lowest, ULONG Highest ) {
    ULONG AllocatedPort;

    Lowest -= PortSet->StartingPort;
    Highest -= PortSet->StartingPort;

    ExAcquireFastMutex( &PortSet->Mutex );
    AllocatedPort = RtlFindClearBits( &PortSet->ProtoBitmap, 1, Lowest );
    if( AllocatedPort != (ULONG)-1 && AllocatedPort >= Lowest &&
        AllocatedPort <= Highest) {
	RtlSetBit( &PortSet->ProtoBitmap, AllocatedPort );
	AllocatedPort += PortSet->StartingPort;
    }
    ExReleaseFastMutex( &PortSet->Mutex );

    AllocatedPort = htons(AllocatedPort);

    return AllocatedPort;
}
