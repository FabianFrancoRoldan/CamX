#include "songmain.h"
#include "MIDIoutdevice.h"
#include "MIDIhardware.h"
//#include "MIDIcontrolmap.h"
#include "camxfile.h"
#include "settings.h"
#include "editdata.h"
#include "gui.h"
#include "object_song.h"
#include "object_project.h"
#include "chunks.h"
#include "MIDIoutproc.h"
#include "audiohardware.h"

MIDIOutputDevice::MIDIOutputDevice()
{
	init=0;
	hMIDIOut=0;

	lockMIDIsongposition=lockMIDIstartstop=lockMIDIclock=lockmtc=false;
	monitor_syscounter=monitor_eventcounter=0; // no output
	userinfo[0]=0; // No User Info
	name=initname=fullname=0;
	synccounter=0;
	datareadbuffercounter=datawritebuffercounter=0; // MIDI Data Ringbuffer
	displaynoteoff_monitor=false;

	for(int i=0;i<DATABUFFERSIZE;i++)datasysex[i]=0;
}

void MIDIOutFilter::ResetFilter()
{
	for(int i=0;i<16;i++){
		programs[i].program=channelpressure[i].pressure=polypressure[i].key=pitchbend[i].lsb=255;	
		for(int a=0;a<128;a++)controller[i][a]=255;
	}
}

#define HNIBBLE 0xF0
#define LNIBBLE 0x0F

void MIDIOutputDevice::sendSongPosition_MTCRealtime(Seq_Song *song)
{
	switch(mtcqf.rotation)
	{
	case MTC_Quarterframe::SENDNEXTMTCQF_FRAME:
		{
			UBYTE mtc[2],l;

			mtc[0]=0xF1;

			mtcqf.rotation=MTC_Quarterframe::SENDNEXTMTCQF_FRAME_H;

			l=(UBYTE)mtcqf.frame;
			l&=LNIBBLE;

			mtc[1]=l;

			LockDevice();
			sendData_NL(mtc,2); // Frame low
			UnlockAndSend();
		}
		break;

	case MTC_Quarterframe::SENDNEXTMTCQF_FRAME_H:
		{
			UBYTE mtc[2],h;

			mtc[0]=0xF1;

			mtcqf.rotation=MTC_Quarterframe::SENDNEXTMTCQF_SEC;

			h=(UBYTE)mtcqf.frame;
			h&=HNIBBLE;

			h>>=4;
			mtc[1]=16|(h&1); //... 000f

			LockDevice();
			sendData_NL(mtc,2); // Frame high
			UnlockAndSend();
		}
		break;

	case MTC_Quarterframe::SENDNEXTMTCQF_SEC:
		{
			UBYTE mtc[2],l;

			mtc[0]=0xF1;

			mtcqf.rotation=MTC_Quarterframe::SENDNEXTMTCQF_SEC_H;

			l=(UBYTE)mtcqf.sek;
			l&=LNIBBLE;

			mtc[1]=32|l;

			LockDevice();
			sendData_NL(mtc,2); // sec low
			UnlockAndSend();
		}
		break;

	case MTC_Quarterframe::SENDNEXTMTCQF_SEC_H:
		{
			UBYTE mtc[2],h;

			mtc[0]=0xF1;

			mtcqf.rotation=MTC_Quarterframe::SENDNEXTMTCQF_MIN;

			h=(UBYTE)mtcqf.sek;
			h&=HNIBBLE;

			h>>=4;
			mtc[1]=48|(h&3); //... 00mm

			LockDevice();
			sendData_NL(mtc,2); // sec high
			UnlockAndSend();
		}
		break;

	case MTC_Quarterframe::SENDNEXTMTCQF_MIN:
		{
			UBYTE mtc[2],l;

			mtc[0]=0xF1;

			mtcqf.rotation=MTC_Quarterframe::SENDNEXTMTCQF_MIN_H;

			l=(UBYTE)mtcqf.min;
			l&=LNIBBLE;

			mtc[1]=64|l;

			LockDevice();
			sendData_NL(mtc,2);
			UnlockAndSend();
		}
		break;

	case MTC_Quarterframe::SENDNEXTMTCQF_MIN_H:
		{
			UBYTE mtc[2],h;

			mtc[0]=0xF1;

			mtcqf.rotation=MTC_Quarterframe::SENDNEXTMTCQF_HOUR;

			h=(UBYTE)mtcqf.min;
			h&=HNIBBLE;

			h>>=4;
			mtc[1]=80|(h&3); //.... 00mm

			LockDevice();
			sendData_NL(mtc,2);
			UnlockAndSend();
		}
		break;

	case MTC_Quarterframe::SENDNEXTMTCQF_HOUR:
		{
			UBYTE mtc[2],l;

			mtc[0]=0xF1;

			mtcqf.rotation=MTC_Quarterframe::SENDNEXTMTCQF_HOUR_H;

			l=(UBYTE)mtcqf.hour;
			l&=HNIBBLE;
			mtc[1]=96|l;

			LockDevice();
			sendData_NL(mtc,2);
			UnlockAndSend();
		}
		break;

	case MTC_Quarterframe::SENDNEXTMTCQF_HOUR_H:
		{
			UBYTE mtc[2],h;

			mtc[0]=0xF1;

			mtcqf.rotation=MTC_Quarterframe::SENDNEXTMTCQF_FRAME;

			h=(UBYTE)mtcqf.hour;
			h&=HNIBBLE;

			h>>=4;
			h&=1; 

			// SMPTE Type
			switch(song->project->standardsmpte)
			{
			case Seq_Pos::POSMODE_SMPTE_24:

				break;

			case Seq_Pos::POSMODE_SMPTE_25:
				h|=1<<1;
				break;

			case Seq_Pos::POSMODE_SMPTE_2997:
				h|=2<<1;
				break;

				//case Seq_Pos::POSMODE_SMPTE_30:
			default: // 30 fps			
				h|=3<<1;
				break;
			}

			mtc[1]=112|h;

			LockDevice();
			sendData(mtc,2);
			UnlockAndSend();
		}
		break;
	}
}

void MIDIOutputDevice::UnlockAndSend()
{
	UnlockDevice();
	MIDIoutproc->SetSignal();
}

void MIDIOutputDevice::sendSongPosition(OSTART ticks)
{
	sendSmallDataRealtime(MIDIREALTIME_STOP);

	// 14 BIT LSB/MSB
	OSTART h=ticks;

	h/=SAMPLESPERBEAT/4;

	OSTART msb_h=h/128,lsb_h=h-(128*msb_h);

	UBYTE bytes[3];

	bytes[0]=0xF2; // songposition
	bytes[1]=(UBYTE)lsb_h;
	bytes[2]=(UBYTE)msb_h;

	sendData(bytes,3);
}

void MIDIOutputDevice::SendDeviceReset(bool force)
{
	for(int i=0;i<16;i++)
	{
		sendControlChange(CONTROLCHANGE|i,7,127); // Volume=127
		sendControlChange(CONTROLCHANGE|i,64,0); // Hold1 off
		sendControlChange(CONTROLCHANGE|i,69,0); // Hold2 of
		sendControlChange(CONTROLCHANGE|i,8,64); // Pan Mid
		sendPitchbend(PITCHBEND|i,0,64);
	}
}

void MIDIOutputDevice::SendOutput()
{
	while(datareadbuffercounter!=datawritebuffercounter){

		LockDevice();

		if(datasysex[datareadbuffercounter])
		{
			char *data=datasysex[datareadbuffercounter];
			int length=datasysexlen[datareadbuffercounter];

			UnlockDevice();

			//SysEx
#ifdef WIN32
			MIDIHDR MIDIouthdr;

			MIDIouthdr.lpData = data; //(LPBYTE)&b[0];
			MIDIouthdr.dwBufferLength = length;
			MIDIouthdr.dwFlags = 0;

			MMRESULT err = midiOutPrepareHeader(hMIDIOut,  &MIDIouthdr, sizeof(MIDIHDR));

			if (err==MMSYSERR_NOERROR)
			{
				//Output the SysEx message. Note that this could return immediately if the device driver can output the message
				//on its own in the background. Otherwise, the driver may make us wait in this call until the entire data is output
				err = midiOutLongMsg(hMIDIOut, &MIDIouthdr, sizeof(MIDIHDR));

				if (err==MMSYSERR_NOERROR)
				{
					// UnpRepare the buffer and MIDIHDR
					while (MIDIERR_STILLPLAYING == midiOutUnprepareHeader(hMIDIOut, &MIDIouthdr, sizeof(MIDIHDR)))
					{
						// Delay to give it time to finish
						Sleep(1);
					}

				}
			}

			LockDevice();
			datasysex[datareadbuffercounter]=0; // Reset
			UnlockDevice();

			delete data; // Free SysEx Data Buffer
#endif
		}
		else
		{
			UnlockDevice();

			//Standard
#ifdef WIN32
			midiOutShortMsg(hMIDIOut,databuffer[datareadbuffercounter].dwData);
#endif
		}

		datareadbuffercounter==DATABUFFERSIZE-1?datareadbuffercounter=0:datareadbuffercounter++;
	}
}

void MIDIOutputDevice::sendBigData(UBYTE *data,int length)
{
	if((!data) || length<=0 || init==0)return;

	if(char *ndata=new char[length])
	{
		memcpy(ndata,data,length);

		LockDevice();

		if(!datasysex[datawritebuffercounter]){
			datasysex[datawritebuffercounter]=ndata;
			datasysexlen[datawritebuffercounter]=length;

#ifdef DEBUG
			if(mainvar->GetActiveSong())
				datatime[datawritebuffercounter]=mainvar->GetActiveSong()->GetSongPosition();
#endif

			datawritebuffercounter==DATABUFFERSIZE-1?datawritebuffercounter=0:datawritebuffercounter++;

			UnlockDevice();

			MIDIoutproc->SetSignal();
		}
		else{
			UnlockDevice();
			delete ndata; // SysEx Buffer in use !
		}
	}
}

void MIDIOutputDevice::sendData_NL(UBYTE *data,int length)
{
	if(init==0)return;

#ifdef WIN32

	databuffer[datawritebuffercounter].bData[0]=*data++;
	databuffer[datawritebuffercounter].bData[1]=length>=2?*data++:0;
	databuffer[datawritebuffercounter].bData[2]=length>=3?*data:0;
#endif

	if(datawritebuffercounter==DATABUFFERSIZE-1)datawritebuffercounter=0;else datawritebuffercounter++;
}

void MIDIOutputDevice::sendSmallData(UBYTE *data,int length)
{
	if(init==0)return;

	LockDevice();

#ifdef WIN32

	databuffer[datawritebuffercounter].bData[0]=*data++;
	databuffer[datawritebuffercounter].bData[1]=length>=2?*data++:0;
	databuffer[datawritebuffercounter].bData[2]=length>=3?*data:0;

#ifdef DEBUG
	//	if(mainvar->GetActiveSong())
	//		datatime[datawritebuffercounter]=mainvar->GetActiveSong()->GetSongPosition();
#endif

#endif

	if(datawritebuffercounter==DATABUFFERSIZE-1)datawritebuffercounter=0; else datawritebuffercounter++;

	UnlockDevice();
	MIDIoutproc->SetSignal();
}

void MIDIOutputDevice::sendSmallDataRealtime(UBYTE byte)
{
	sendSmallData(&byte,1);
}

void MIDIOutputDevice::sendNote(UBYTE status,UBYTE key,UBYTE velo)
{	
	if(velo!=0)
	{
		LockMonitor();
		monitor_events[monitor_eventcounter].Init(status,key,velo,3,0);
		if(monitor_eventcounter==MAXMONITOREVENTS-1)monitor_eventcounter=0; else monitor_eventcounter++;
		UnlockMonitor();
	}
	else 
		if(displaynoteoff_monitor==true)
		{
			LockMonitor();
			monitor_events[monitor_eventcounter].Init(NOTEOFF|(status&0x0F),key,0,3,0);
			if(monitor_eventcounter==MAXMONITOREVENTS-1)monitor_eventcounter=0; else monitor_eventcounter++;
			UnlockMonitor();
		}

		UBYTE data[3];

		data[0]=status;
		data[1]=key;
		data[2]=velo;

		sendData(data,3);
}

void MIDIOutputDevice::sendNoteOff(UBYTE status,UBYTE key,UBYTE velooff)
{
	if(displaynoteoff_monitor==true)
	{
		LockMonitor();
		monitor_events[monitor_eventcounter].Init(NOTEOFF|(status&0x0F),key,velooff,3,0);
		if(monitor_eventcounter==MAXMONITOREVENTS-1)monitor_eventcounter=0; else monitor_eventcounter++;
		UnlockMonitor();
	}

	UBYTE data[3];

	data[0]=status;
	data[1]=key;
	data[2]=velooff;

	sendData(data,3);
}

void MIDIOutputDevice::sendPolyPressure(UBYTE status,UBYTE key,UBYTE pressure)
{
	LockMonitor();
	monitor_events[monitor_eventcounter].Init(status,key,pressure,3,0);
	if(monitor_eventcounter==MAXMONITOREVENTS-1)monitor_eventcounter=0; else monitor_eventcounter++;
	UnlockMonitor();

	UBYTE data[3];

	data[0]=status;
	data[1]=key;
	data[2]=pressure;

	sendData(data,3);
}

void MIDIOutputDevice::sendControlChange(UBYTE status,UBYTE controller,UBYTE value)
{		
	LockMonitor();
	monitor_events[monitor_eventcounter].Init(status,controller,value,3,0);
	if(monitor_eventcounter==MAXMONITOREVENTS-1)monitor_eventcounter=0; else monitor_eventcounter++;
	UnlockMonitor();

	UBYTE data[3];

	data[0]=status;
	data[1]=controller;
	data[2]=value;

	sendData(data,3);
}

void MIDIOutputDevice::sendPitchbend(UBYTE status,UBYTE lsb,UBYTE msb)
{	
	LockMonitor();
	monitor_events[monitor_eventcounter].Init(status,lsb,msb,3,0);
	if(monitor_eventcounter==MAXMONITOREVENTS-1)monitor_eventcounter=0; else monitor_eventcounter++;
	UnlockMonitor();

	UBYTE data[3];

	data[0]=status;
	data[1]=lsb;
	data[2]=msb;	

	sendData(data,3);
}

void MIDIOutputDevice::sendProgramChange(UBYTE status,UBYTE program)
{		
	LockMonitor();
	monitor_events[monitor_eventcounter].Init(status,program,0,2,0);
	if(monitor_eventcounter==MAXMONITOREVENTS-1)monitor_eventcounter=0; else monitor_eventcounter++;
	UnlockMonitor();

	UBYTE data[2];

	data[0]=status;
	data[1]=program;
	sendData(data,2);
}

void MIDIOutputDevice::sendChannelPressure(UBYTE status,UBYTE pressure)
{	
	LockMonitor();
	monitor_events[monitor_eventcounter].Init(status,pressure,0,2,0);
	if(monitor_eventcounter==MAXMONITOREVENTS-1)monitor_eventcounter=0; else monitor_eventcounter++;
	UnlockMonitor();

	UBYTE data[2];

	data[0]=status;
	data[1]=pressure;

	sendData(data,2);
}

void MIDIOutputDevice::sendSysEx(UBYTE *data,int length)
{	
	if(length>0 && device_eventfilter.CheckBytes(0xF0,0,0)==true)
	{
		int a=length>MAXMONITORBYTES?MAXMONITORBYTES:length;

		LockMonitor();

		for(int i=0;i<a;i++)
			monitor_events[monitor_eventcounter].data[i]=data[i];

		monitor_events[monitor_eventcounter].data_l[0]=length;
		monitor_events[monitor_eventcounter].data_l_length=1;
		monitor_events[monitor_eventcounter].datalength=length;

		if(monitor_eventcounter==MAXMONITOREVENTS-1)monitor_eventcounter=0; else monitor_eventcounter++;
		UnlockMonitor();

		if(length>3)
			sendBigData(data,length);
		else
			sendSmallData(data,length);
	}
}

int MIDIOutputDevice::OpenOutputDevice(int mode) // open, init MIDI-Device
{
	if(!init)
	{		
#ifdef WIN32
		MMRESULT res=MMSYSERR_ERROR;

		try
		{
			res=midiOutOpen(&hMIDIOut, id, 0, 0, CALLBACK_NULL);
		}

		catch(...)
		{
		}

		maingui->MessageMMError(FullName(),"MIDI Output Device (Open)",res);

		if(res==MMSYSERR_NOERROR)
		{
			//printf("Device %d open \n",id);
			// MIDIOutReset(hMIDIOut);
			init=1;
			SendDeviceReset(true);
		}
#endif
	}

	return 0;
}

void MIDIOutputDevice::CreateFullName()
{
	if(fullname)delete fullname;
	fullname=mainvar->GenerateString(name,">",userinfo);
}

void MIDIOutputDevice::CloseOutputDevice()
{
	if(fullname)delete fullname;
	fullname=0;

	if(initname)delete initname;
	initname=0;

	if(name)delete name;
	name=0;

	if(init){
		init=0;

#ifdef WIN32
		LockDevice();
		if(hMIDIOut)
		{
			MMRESULT res=MMSYSERR_ERROR;

			try
			{
				res=midiOutClose(hMIDIOut);
			}

			catch(...)
			{
			}

			maingui->MessageMMError(FullName(),"midiOutClose",res);
		}

		hMIDIOut=0;
		UnlockDevice();
#endif
	}
}

void MIDIOutputProgram::Load(camxFile *file)
{
	file->LoadChunk();

	if(file->GetChunkHeader()==CHUNK_MIDIPROGRAMNEW)
	{
		file->ChunkFound();

		file->ReadChunk(&MIDIBank_lsb);
		file->ReadChunk(&MIDIProgram);
		file->ReadChunk(&usebank_lsb);
		file->ReadChunk(&useprogramchange);
		file->ReadChunk(&MIDIBank_msb);
		file->ReadChunk(&usebank_msb);
		file->ReadChunk(&channel);

		file->CloseReadChunk();
	}
}

void MIDIOutputProgram::Save(camxFile *file)
{
	file->OpenChunk(CHUNK_MIDIPROGRAMNEW);

	file->Save_Chunk(MIDIBank_lsb);
	file->Save_Chunk(MIDIProgram);
	file->Save_Chunk(usebank_lsb);
	file->Save_Chunk(useprogramchange);
	file->Save_Chunk(MIDIBank_msb);
	file->Save_Chunk(usebank_msb);
	file->Save_Chunk(channel);

	file->CloseChunk();
}

void MIDIOutputProgram::Clone(MIDIOutputProgram *to)
{
	to->MIDIProgram=MIDIProgram;

	to->MIDIBank_msb=MIDIBank_msb;
	to->MIDIBank_lsb=MIDIBank_lsb;

	to->usebank_msb=usebank_msb;
	to->usebank_lsb=usebank_lsb;

	to->useprogramchange=useprogramchange;
	to->channel=channel;
}

bool MIDIOutputProgram::Compare(MIDIOutputProgram *to)
{
	if(
		to->MIDIProgram!=MIDIProgram ||
		to->MIDIBank_lsb!=MIDIBank_lsb ||
		to->MIDIBank_msb!=MIDIBank_msb ||
		to->usebank_lsb!=usebank_lsb ||
		to->useprogramchange!=useprogramchange ||
		to->channel!=channel
		)
		return false;

	return true;
}
