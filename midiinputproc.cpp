#include "songmain.h"
#include "MIDIinproc.h"
#include "MIDIhardware.h"
#include "MIDIoutproc.h"
#include "semapores.h"
#include "audiohardware.h"
#include "settings.h"
#include "mainhelpthread.h"
#include "object_song.h"
#include "object_track.h"
#include "MIDItimer.h"
#include "MIDIthruproc.h"
#include "MIDIprocessor.h"

#ifdef DEBUG
#include "gui.h"
#endif

bool OpenNoteFilter::CheckNote(UBYTE status,char key)
{
	int echannel=status&0x0F;

	echannel=1<<echannel;

	if(channel&echannel)
		return true;

	return false;
}

bool Seq_Song::FindOpenNote(int key,OpenNoteFilter *filter)
{
	// Check Realtimes
	for(int i=0;i<2;i++)
	{
		realtimeevents[i].Lock();

		for(int i2=0;i2<2;i2++)
		{
			RealtimeEvent *re=i2==0?realtimeevents[i].FirstREvent():realtimeevents[i].FirstCDEvent();
			while(re)
			{
				switch(re->realtimeid)
				{
				case REALTIMEID_NOTEOFFREALTIME:
					{
						NoteOff_Realtime *nor=(NoteOff_Realtime *)re;

						if((nor->key==key || key==-1) && ((!filter) || filter->CheckNote(nor->status,nor->key)==true))
						{
							realtimeevents[i].UnLock();
							return true;
						}

					}
					break;
				}

				re=re->NextEvent();
			}
		}

		realtimeevents[i].UnLock();
	}

	// Check Thru
	return mainMIDIthruthread->FindOpenKey(key,filter);
}

void Seq_Song::SysexEnd(NewEventData *input)
{
	if(input->fromdev && input->fromdev->incomingstream)
	{
		TRACE ("Sysex End %d\n",input->fromdev->incomingstreamlength);

		if((status&(Seq_Song::STATUS_STEPRECORD|Seq_Song::STATUS_RECORD)) &&
			mainsettings->recording_MIDI==true &&
			MIDIrecording==true) // Record Mode ?
		{
			if(NewEventData *mid=new NewEventData)
			{
				mid->song=this;
				mid->songstatusatinput=status;
				mid->fromdev=input->fromdev;
				mid->fromplugin=input->fromplugin;

				mid->netime=input->fromdev->incomingstream_starttime;
				mid->status=0xF0;
				mid->byte1=0;
				mid->byte2=0;
				mid->data=input->fromdev->incomingstream;
				mid->datalength=input->fromdev->incomingstreamlength;

				input->fromdev->incomingstream=0;

				mainMIDIrecord->AddMIDIRecordEvent(mid); // Add To Record
			}
		}

		if(input->fromdev->incomingstream)
			delete input->fromdev->incomingstream;

		input->fromdev->incomingstream=0;
		input->fromdev->incomingstreamlength=0;
	}
}

void Seq_Song::NewEventInput(NewEventData *input)
{
	if(mainMIDI->MIDI_inputimpulse<=50)
		mainMIDI->MIDI_inputimpulse=100;

	bool addnewevent=true,record;

	if( (input->songstatusatinput&STATUS_STEPRECORD) || (input->songstatusatinput&STATUS_RECORD))
		record=true;
	else
		record=false;

	if(CheckStatus(input->status&0xF0)==true)
	{
		record=true;
		input->netime=songstartposition;
	}

	if(input->fromdev && input->fromdev->incomingstream && (input->status!=0xF0))
	{
		SysexEnd(input);
		addnewevent=false;
	}
	else
		// SysEx Check
		switch(input->status&0xF0)
	{	
		case 0xF0:
			{
				addnewevent=false;

				if(input->data && input->datalength)
				{
					// Data
					if(input->datalength>=2 && input->data[1]==0x7E) // Non-Realtime Messages
					{
						addnewevent=false;
					} // end Non Realtime Message
					else
						if(input->datalength>=2 && input->data[1]==0x7F) // Real time Messages
						{
							addnewevent=false;

							// MTC Full Message ?
							if(input->datalength==10 && input->data[3]==0x01 && input->data[4]==0x01) // data[2]==0x7F (All Channels)
							{
								UBYTE type=(input->data[5]&0x60)>>5, // 0xx00000
									hour=input->data[5]&0x1F, // 00011111
									min=input->data[6],
									sec=input->data[7],
									frames=input->data[8];

								// Correction
								if(hour>23)
									hour=23;

								if(min>59)
									min=59;

								if(sec>59)
									sec=59;

								OSTART ticks=(mainaudio->GetGlobalSampleRate()*3600*hour)+(mainaudio->GetGlobalSampleRate()*60*min)+(mainaudio->GetGlobalSampleRate()*sec);

								switch(type)
								{
								case 0: // 24F
									{
										if(frames>23)frames=23;

										double h=mainaudio->GetGlobalSampleRate();
										h/=24;
										h*=frames;

										ticks+=(int)h;
									}
									break;

								case 1: // 25f
									{
										if(frames>24)
											frames=24;

										double h=mainaudio->GetGlobalSampleRate();
										h/=25;
										h*=frames;

										ticks+=(int)h;
									}
									break;

								case 2: // 30 DF
									{
										if(frames>29)
											frames=29;

										double h=mainaudio->GetGlobalSampleRate();
										h/=29.97;
										h*=frames;

										ticks+=(int)h;
									}
									break;

								case 3: // 30f
									{
										if(frames>29)
											frames=29;

										double h=mainaudio->GetGlobalSampleRate();
										h/=30;
										h*=frames;

										ticks+=(int)h;
									}
									break;
								}

								if(input->fromdev)
								{
									input->fromdev->indesongposition=ticks;
									input->fromdev->newsongpositionset=true;
								}

								mainsyncthread->Lock();
								newsongpositionset=true;
								mainsyncthread->Unlock();

								mainsyncthread->SetSignal();

							}// end MTC Full Message
							else
								if(input->datalength==15 && input->data[3]==0x01 && input->data[4]==0x02)
								{
									// MIDI Time Code User Bit Message

								}// end MTC User Bit
								else
									if(input->datalength==5 && input->data[2]==0x7E && input->data[3]==0x7E)
									{
										// NAK
										if(input->fromdev)
										{
											input->fromdev->mtcinput.Reset();
										}
									}


						}//end Realtime Messages
						else
						{
							addnewevent=false;

							// Standard Data Block
							TRACE ("Incoming Standard Data Block Status %d %d %d\n",input->status,input->byte1,input->datalength);

							if(input->byte1==1) // New SysEx Stream
							{
								TRACE ("Header\n");

								if(input->fromdev)
								{
									if(input->fromdev->incomingstream)
										delete input->fromdev->incomingstream;

									input->fromdev->incomingstream_starttime=input->netime;
									input->fromdev->incomingstream=input->data;
									input->fromdev->incomingstreamlength=input->datalength;
								}

								input->data=0;
							}
							else // Add to Stream
							{
								if(input->fromdev && input->fromdev->incomingstream)
								{
									TRACE ("Add to Header\n");

									UBYTE *newdata=new UBYTE[input->fromdev->incomingstreamlength+input->datalength];

									if(newdata)
									{
										memcpy(newdata,input->fromdev->incomingstream,input->fromdev->incomingstreamlength);
										memcpy(newdata+input->fromdev->incomingstreamlength,input->data,input->datalength); // Add
										input->fromdev->incomingstreamlength=input->fromdev->incomingstreamlength+input->datalength;
									}
									else
										input->fromdev->incomingstreamlength=0;

									delete input->fromdev->incomingstream;

									input->fromdev->incomingstream=newdata;

									TRACE ("Header new Size %d\n",input->fromdev->incomingstreamlength);
								}
							}

							// Check For SysEx End
							if(input->fromdev && input->fromdev->incomingstream)
							{
								UBYTE *check=input->fromdev->incomingstream;

								for(int i=0;i<input->fromdev->incomingstreamlength;i++)
								{
									if(*check==0xF7)
									{
										SysexEnd(input);
										break;
									}

									check++;
								}

							}

						}
				} // Data

			}//0xF0
			break;
	}// switch

	// Add to Thru Thread
	if(NewEventData *thru=new NewEventData)
	{
		memcpy(thru,input,sizeof(NewEventData));

		if(input->data) // SysEx Data
		{
			if(thru->data=new UBYTE[input->datalength])
				memcpy(thru->data,input->data,input->datalength);
		}

		mainMIDIthruthread->LockInput();
		mainMIDIthruthread->thruevents.AddEndO(thru);
		mainMIDIthruthread->UnlockInput();

		mainMIDIthruthread->SetSignal();
	}

	if(record==true && mainsettings->recording_MIDI==true && addnewevent==true && MIDIrecording==true) // Record Mode ?
	{
		if(NewEventData *rec=new NewEventData){

			memcpy(rec,input,sizeof(NewEventData)); // +data,datalength!

			input->data=0; // clear data, copyied to rec->data !
			rec->song=this;

			mainMIDIrecord->AddMIDIRecordEvent(rec); // Add To Record
			// data+datalength -> rec class
			return;
		}
	}

	// Delete Data
	if(input->data)
	{
		delete input->data;
		input->data=0;
	}
}

void MIDIInputDevice::NewMTCQF(Seq_Song *song,NewEventData *input)
{
	UBYTE type=(input->byte1&0x70)>>4;
	UBYTE nibble=(input->byte1&0x0F);
	bool clearflag=false;

	switch(type)
	{
		// Frame 
	case 0:
		TRACE ("FRAME LOW \n");
		mtcinput.frame_ls=nibble;
		mtcinput.inputflag|=MTC_Quarterframe::IN_FRAME_LN;
		break;

	case 1:
		TRACE ("FRAME HIGH \n");

		mtcinput.frame_ms=nibble /*&0x01*/;
		mtcinput.inputflag|=MTC_Quarterframe::IN_FRAME;
		break;

		// Sec
	case 2:
		TRACE ("SEC LOW \n");

		mtcinput.sec_ls=nibble;
		mtcinput.inputflag|=MTC_Quarterframe::IN_SEC_LN;
		mtcinput.inputflag CLEARBIT MTC_Quarterframe::IN_SEC;
		break;

	case 3:
		TRACE ("SEC HIGH \n");

		mtcinput.sec_ms=nibble /*&0x03*/;
		mtcinput.inputflag|=MTC_Quarterframe::IN_SEC;
		break;

		// Min
	case 4:
		TRACE ("MIN LOW \n");

		mtcinput.min_ls=nibble;
		mtcinput.inputflag|=MTC_Quarterframe::IN_MIN_LN;
		mtcinput.inputflag CLEARBIT MTC_Quarterframe::IN_MIN;
		break;

	case 5:
		TRACE ("MIN HIGH \n");

		mtcinput.min_ms=nibble /*&0x03*/;
		mtcinput.inputflag|=MTC_Quarterframe::IN_MIN;
		break;

		// Hour
	case 6:

		TRACE ("HOUR LOW \n");

		mtcinput.hour_ls=nibble;
		mtcinput.inputflag|=MTC_Quarterframe::IN_HOUR_LN;
		mtcinput.inputflag CLEARBIT MTC_Quarterframe::IN_HOUR;
		break;

	case 7:
		TRACE ("HOUR HIGH \n");

		// 0111 0rrh 	Rate and hour msbit
		mtcinput.hour_ms=nibble&0x01;
		mtcinput.timecodetype=(nibble&0x06)>>1;
		mtcinput.inputflag|=MTC_Quarterframe::IN_HOUR;

		clearflag=true;

		break;
	}

	// All MTC Filled ?
	if(mtcinput.inputflag==
		(
		MTC_Quarterframe::IN_FRAME_LN|MTC_Quarterframe::IN_FRAME|
		MTC_Quarterframe::IN_SEC_LN|MTC_Quarterframe::IN_SEC|
		MTC_Quarterframe::IN_MIN_LN|MTC_Quarterframe::IN_MIN|
		MTC_Quarterframe::IN_HOUR_LN|MTC_Quarterframe::IN_HOUR
		)
		)
	{
		// 8 Bit 2x nibble
		{
			UBYTE h8=mtcinput.hour_ls|(mtcinput.hour_ms<<4);
			UBYTE m8=mtcinput.min_ls|(mtcinput.min_ms<<4);
			UBYTE s8=mtcinput.sec_ls|(mtcinput.sec_ms<<4);
			UBYTE f8=mtcinput.frame_ls|(mtcinput.frame_ms<<4);

			mtcinput.hour=h8;
			mtcinput.min=m8;
			mtcinput.sek=s8;
			mtcinput.frame=f8;
		}

		switch(mtcinput.timecodetype)
		{
		case 0:
			{
				Seq_Pos pos(Seq_Pos::POSMODE_SMPTE_24);

				pos.pos[0]=mtcinput.hour;
				pos.pos[1]=mtcinput.min;
				pos.pos[2]=mtcinput.sek;
				pos.pos[3]=mtcinput.frame;
				pos.pos[4]=0;// No QF

				mtcinput.ticks=song->timetrack.ConvertPosToTicks(&pos);

				TRACE ("MTC 24f %d %d %d %d \n",mtcinput.hour,mtcinput.min,mtcinput.sek,mtcinput.frame);
			}
			// 24F
			break;

		case 1:
			{
				Seq_Pos pos(Seq_Pos::POSMODE_SMPTE_25);

				pos.pos[0]=mtcinput.hour;
				pos.pos[1]=mtcinput.min;
				pos.pos[2]=mtcinput.sek;
				pos.pos[3]=mtcinput.frame;
				pos.pos[4]=0;// No QF

				TRACE ("MTC 25f %d %d %d %d \n",mtcinput.hour,mtcinput.min,mtcinput.sek,mtcinput.frame);

				mtcinput.ticks=song->timetrack.ConvertPosToTicks(&pos);
				// 25F
			}
			break;

		case 2:
			{
				Seq_Pos pos(Seq_Pos::POSMODE_SMPTE_2997);

				pos.pos[0]=mtcinput.hour;
				pos.pos[1]=mtcinput.min;
				pos.pos[2]=mtcinput.sek;
				pos.pos[3]=mtcinput.frame;
				pos.pos[4]=0;// No QF

				mtcinput.ticks=song->timetrack.ConvertPosToTicks(&pos);
			}
			// 30DF/29.97
			break;

		case 3:
			{
				Seq_Pos pos(Seq_Pos::POSMODE_SMPTE_30);

				pos.pos[0]=mtcinput.hour;
				pos.pos[1]=mtcinput.min;
				pos.pos[2]=mtcinput.sek;
				pos.pos[3]=mtcinput.frame;
				pos.pos[4]=0;// No QF

				TRACE ("MTC 30f %d %d %d %d \n",mtcinput.hour,mtcinput.min,mtcinput.sek,mtcinput.frame);

				mtcinput.ticks=song->timetrack.ConvertPosToTicks(&pos);
			}
			//30F
			break;

		default:
			mtcinput.ticks=-1; // Unknown Type
			break;
		}// switch

		if(mtcinput.ticks!=-1)
		{
		}
	}

	if(clearflag==true)
		mtcinput.inputflag=0; // Reset QF Flags

}

void MIDIInputDevice::NewMIDIClock(Seq_Song *song,NewEventData *input)
{
	inputMIDIclockcounter++;

	if(inputMIDIclockcounter==4) // 4 wait clocks,Set 1. Checkpoint
	{
		Seq_Tempo *tempo=song->timetrack.GetTempo(input->netime);

		// Init Tempo+Ticks
		lastMIDIclock_songposition=input->netime;
		lastMIDIclock_systime=input->nesystime;

		MIDIclocktempo=tempo->tempo;
		inputMIDIclockcounter=8;
		tempobuffercounter=0;
	}
	else
		if(inputMIDIclockcounter>4) // Init ?
		{	
			LONGLONG systicks=input->nesystime-lastMIDIclock_systime;
			lastMIDIclock_systime=input->nesystime;

			//MS 500ms=1/4 120 BPM
			double ticks=maintimer->ConvertSysTimeToInternTicks(systicks);
			double h=SAMPLESPERBEAT;

			ticks/=h;
			ticks*=24;

			if(ticks>0)
			{
				if(systicks==0)
				{
					// Tempo out of range, connection lag etc..
					// Reset
					inputMIDIclockcounter=0;
				}
				else
				{
					double MIDI_tempo=120/ticks;

					if(MIDI_tempo<MINIMUM_TEMPO || MIDI_tempo>=MAXIMUM_TEMPO)
					{
						// Tempo out of range, connection lag etc..
						// Reset
						inputMIDIclockcounter=0;
					}
					else
					{
						tempobuffer[tempobuffercounter++]=MIDI_tempo;

						if(tempobuffercounter==8) // Wait 8 Buffer
						{
							tempobuffercounter=0;

							MIDI_tempo=0;

							for(int i=0;i<8;i++)
								MIDI_tempo+=tempobuffer[i];

							MIDI_tempo/=8;

							if(MIDI_tempo!=MIDIclocktempo)
							{
								double diff=MIDI_tempo>MIDIclocktempo?MIDI_tempo-MIDIclocktempo:MIDIclocktempo-MIDI_tempo;

								MIDIclocktempo=MIDI_tempo;

								if(diff>minMIDIclockdifference){

									if(MIDIclockraster!=-1) // Quantize Tempo Position ?
										input->netime=mainvar->SimpleQuantize(input->netime,quantlist[MIDIclockraster]);

									bool portwithsync=false;

									for(int i=0;i<MAXMIDIPORTS;i++)
									{
										if(mainMIDI->MIDIinports[i].visible==true && mainMIDI->MIDIinports[i].inputdevice==this && mainMIDI->MIDIinports[i].receivesync==true)
										{
											portwithsync=true;
											break;
										}
									}

									if(portwithsync==true)
									{
										if(song->ChangeTempoAtPosition(input->netime,MIDI_tempo,TEMPOREFRESH_NOGUI|TEMPOREFRESH_NOLOOPREFRESH)==true)
										{
											song->LockExternSync();
											song->newexterntempochange=true;
											song->UnlockExternSync();
										}
									}

									//if(timetrack.AddNewTempo(TEMPOEVENT_REAL,input->netime,tempo))
									//	timetrack.newMIDIclocktempo_record=true;
								}
							}

						}
					}
				}
			}
		}
}

void Seq_Song::NewMIDIInput(NewEventData *input)
{
	switch(input->status)
	{
	case MIDIREALTIME_CLOCK:
		{
			if(MIDIInputDevice *indev=input->fromdev)
			{
				if(MIDIsync.sync==SYNC_MC || MIDIsync.sync==SYNC_MTC)
					indev->NewMIDIClock(this,input);
			}
		}
		break; // END MIDICLOCK

	case MIDIREALTIME_START:
		if(MIDIsync.sync==SYNC_MC)
		{
			bool portwithsync=false;

			for(int i=0;i<MAXMIDIPORTS;i++)
			{
				if(mainMIDI->MIDIinports[i].visible==true && mainMIDI->MIDIinports[i].inputdevice==input->fromdev && mainMIDI->MIDIinports[i].receivesync==true)
				{
					portwithsync=true;
					break;
				}
			}

			if(portwithsync==true && MIDIsync.startedwithmc==false)
			{
				MIDIsync.startedwithmc=true;

				if(GetSongPosition()!=0) // Set Start Position : 0
				{
					if(MIDIInputDevice *indev=input->fromdev)
					{
						indev->indesongposition=0;
						indev->newsongpositionset=true;
					}

					newsongpositionset=true;
				}

				mainsyncthread->Lock();
				mainsyncthread->startactivesong=true;
				mainsyncthread->Unlock();

				mainsyncthread->SetSignal();
			}
		}
		break;


	case MIDIREALTIME_CONTINUE:
		if(MIDIsync.sync==SYNC_MC)
		{
			bool portwithsync=false;

			for(int i=0;i<MAXMIDIPORTS;i++)
			{
				if(mainMIDI->MIDIinports[i].visible==true && mainMIDI->MIDIinports[i].inputdevice==input->fromdev && mainMIDI->MIDIinports[i].receivesync==true)
				{
					portwithsync=true;
					break;
				}
			}

			if(portwithsync==true && MIDIsync.startedwithmc==false)
			{
				MIDIsync.startedwithmc=true;

				mainsyncthread->Lock();
				mainsyncthread->startactivesong=true;
				mainsyncthread->Unlock();

				mainsyncthread->SetSignal();
			}
		}
		break;

	case MIDIREALTIME_STOP:
		if(MIDIsync.sync==SYNC_MC)
		{
			bool portwithsync=false;

			for(int i=0;i<MAXMIDIPORTS;i++)
			{
				if(mainMIDI->MIDIinports[i].visible==true && mainMIDI->MIDIinports[i].inputdevice==input->fromdev && mainMIDI->MIDIinports[i].receivesync==true)
				{
					portwithsync=true;
					break;
				}
			}

			if(portwithsync==true)
			{
				MIDIsync.startedwithmc=false;

				mainsyncthread->Lock();
				mainsyncthread->stopactivesong=true;
				mainsyncthread->Unlock();

				mainsyncthread->SetSignal();
			}
		}
		break;

	case MIDIREALTIME_SONGPOSITION:
		{
			// The position represents the MIDI beat, where a sequence always starts on beat zero and each beat is a 16th note. 
			// Thus, the device will cue to a specific 16th note.

			// BYTE 1 LSB
			// BYTE 2 MSB
			OSTART spp=input->byte2;
			spp*=128;
			spp+=input->byte1;

			spp*=TICK16nd;

			TRACE ("SPP %d\n",spp);

			if(MIDIInputDevice *indev=input->fromdev)
			{
				indev->indesongposition=spp;
				indev->newsongpositionset=true;
			}

			bool portwithsync=false;

			for(int i=0;i<MAXMIDIPORTS;i++)
			{
				if(mainMIDI->MIDIinports[i].visible==true && mainMIDI->MIDIinports[i].inputdevice==input->fromdev && mainMIDI->MIDIinports[i].receivesync==true)
				{
					portwithsync=true;
					break;
				}
			}

			if(portwithsync==true)
			{
				mainsyncthread->Lock();
				newsongpositionset=true;
				mainsyncthread->Unlock();

				mainsyncthread->SetSignal();
			}
		}
		break; // END SONGPOSITION

	case MIDIREALTIME_MTCQF: // MIDI Time Code Quarter Frame
		{
			bool portwithsync=false;

			for(int i=0;i<MAXMIDIPORTS;i++)
			{
				if(mainMIDI->MIDIinports[i].visible==true && mainMIDI->MIDIinports[i].inputdevice==input->fromdev && mainMIDI->MIDIinports[i].receivemtc==true)
				{
					portwithsync=true;
					break;
				}
			}

			// Add Auto switch to SYNC_MTC

			if(portwithsync==true && MIDIsync.sync==SYNC_MTC)
			{
				if(MIDIsync.startedwithmtc==false)
				{
					// Start with MTC

					MIDIsync.startedwithmtc=true;
					MIDIsync.mtccounter=0;
					MIDIsync.mtccheckstart=0;
					MIDIsync.mtccheckcounter=0;

					mainsyncthread->Lock();
					mainsyncthread->startactivesong=true;
					mainsyncthread->Unlock();

					mainsyncthread->SetSignal();
				}
				else
					MIDIsync.mtccounter++;

				if(MIDIInputDevice *indev=input->fromdev)
				{
					indev->NewMTCQF(this,input);
				}
			}
		}
		break;

	case 0xF3: // Song Select
		break;

	case 0xF4:
	case 0xF5:
	case 0xF9: // non defined
	case 0xFD:						
		break;

	case 0xFE: // Active Sensing
		break;

	case 0xFF: // System Reset
		break;

	default: // MIDI Event
		NewEventInput(input);
		break;
	}
}

void Seq_Song::NewEventInput(NewEventData *input,Seq_Event *e)
{
	NewEventData mid;

	mid.song=this;
	mid.songstatusatinput=status;
	mid.fromdev=input->fromdev;
	mid.fromplugin=input->fromplugin;

	mid.netime=e->GetEventStart();
	mid.nesystime=maintimer->GetSystemTime();

	mid.status=e->status;
	mid.byte1=e->GetByte1();
	mid.byte2=e->GetByte2();
	mid.data=0;
	mid.datalength=0;

	NewEventInput(&mid);
}

void PluginMIDIInputProc::CheckSleepAlarms_SysTime(Seq_Song *song)
{
	NewEventData *se=(NewEventData *)sleepevents.GetRoot();

	if(se)
	{
		LONGLONG timenow=maintimer->GetSystemTime();

		do
		{
			if(se->alarmsystime<=timenow) // Send Sleep Event?
			{
				se->flag|=NED_NOAUDIO;

				song->NewEventInput(se);
				se->FreeData();
				se=(NewEventData *)sleepevents.RemoveO(se); // Remove from Sleep List
			}
			else
				se=(NewEventData *)se->next;

		}while(se);
	}
}

void PluginMIDIInputProc::SendForce(Seq_Song *song,AudioObject *plg)
{
	LockPluginInput();

	NewEventData *se=(NewEventData *)sleepevents.GetRoot();

	while(se)
	{
		if(se->fromplugin==plg) // Send Sleep Event?
		{
			se->flag|=NED_NOAUDIO;

			song->NewEventInput(se);
			se->FreeData();
			se=(NewEventData *)sleepevents.RemoveO(se); // Remove from Sleep List
		}
		else
			se=se->NextEvent();
	}

	UnLockPluginInput();
}

void PluginMIDIInputProc::AddNewInputData(Seq_Song *song,NewEventData *newdata)
{
	if(song)
	{
		newdata->flag|=NED_NOMIDI;
		mainMIDIthruthread->CheckNewEvent(song,newdata,THRUINDEX_PLUGIN); // Thru to other VSTs, no MIDI thru
		newdata->flag CLEARBIT NED_NOMIDI;
	}

	// Add to MIDI Input/Record List
	LockPluginInput();
	vstinputevents.AddEndO(newdata);
	UnLockPluginInput();
}

void PluginMIDIInputProc::WaitLoop()
{
	int i_np=0;

	while(IsExit()==false)
	{
		WaitSignal(i_np); // Wait for incoming Signal
		i_np=0;

		if(IsExit()==true)
			break;

		Lock();

		// Check VST Plugin Input
		LockPluginInput();
		NewEventData *invst=(NewEventData *)FirstEvent();
		UnLockPluginInput();

		if(Seq_Song *asong=mainvar->GetActiveSong())
		{
			CheckSleepAlarms_SysTime(asong);

			while(invst){ // New Events ?

				// invst->netime=songposition
				// invst->deltatimes=sendposition

				if(invst->nedeltatime==invst->netime){
					// Send --> Record etc...

					invst->flag|=NED_NOAUDIO;
					asong->NewEventInput(invst);
					invst->FreeData();

					LockPluginInput();
					invst=(NewEventData *)vstinputevents.RemoveO(invst);
					UnLockPluginInput();
				}
				else {
					// Move to sleep
					//double sleepms=invst->nedeltatime-invst->netime;
					//sleepms*=INTERNRATEMSDIV; // TICKS->MS

					// -> Wait Loop

					//invst->sleepms=sleepms;
					invst->initsystime=maintimer->GetSystemTime();
					invst->alarmsystime=invst->initsystime+maintimer->ConvertInternTicksToSysTime(invst->nedeltatime-invst->netime);
					invst->netime=invst->nedeltatime; // Set new StartPostion

					LockPluginInput();
					NewEventData *next=(NewEventData *)vstinputevents.CutObject(invst);
					UnLockPluginInput();

					// Sort

					NewEventData *se=(NewEventData *)FirstSleepEvent();
					while(se)
					{
						if(se->alarmsystime>invst->alarmsystime)
						{
							sleepevents.AddNextO(invst,se);
							break;
						}

						se=se->NextEvent();
					}

					if(!se)
						sleepevents.AddEndO(invst); // Move to sleep list

					invst=next;
				}
			}

			if(NewEventData *se=(NewEventData *)FirstSleepEvent()){	
				LONGLONG systickswait=se->alarmsystime-se->initsystime;

				double msalarm=maintimer->ConvertSysTimeToMs(systickswait);
#ifdef WIN32
				i_np=(int)ceil(msalarm); // round up 0.6ms>1

				if(i_np<1) // minimun Wait= 1 ms
					i_np=1;
#endif
			}
		}
		else{
			LockPluginInput();
			NewEventData *invst=(NewEventData *)vstinputevents.GetRoot();
			while(invst){
				invst->FreeData();
				invst=(NewEventData *)vstinputevents.RemoveO(invst);
			}
			UnLockPluginInput();
		}

		Unlock();
	}

	// Clear Sleep Events
	NewEventData *se=(NewEventData *)sleepevents.GetRoot();
	while(se)
	{
		se->FreeData();
		se=(NewEventData *)sleepevents.RemoveO(se);
	}

	// Clear VST Input
	NewEventData *invst=(NewEventData *)vstinputevents.GetRoot();
	while(invst)
	{
		invst->FreeData();
		invst=(NewEventData *)vstinputevents.RemoveO(se);
	}

	ThreadGone();
}

PTHREAD_START_ROUTINE PluginMIDIInputProc::PluginInputFunc (LPVOID pParam)
{
	PluginMIDIInputProc *inproc=(PluginMIDIInputProc *)pParam;
	inproc->WaitLoop();
	return 0;
}

void MIDIInProc::WaitLoop()
{
	NewEventData newMIDI;

	while(IsExit()==false)
	{
		// Wait for incoming Signal
		WaitSignal();

		if(IsExit()==true)
			break;

		Lock();

		// MIDI Input Message
		if(Seq_Song *asong=mainvar->GetActiveSong()){

			MIDIInputDevice *indev=mainMIDI->FirstMIDIInputDevice();

			while(indev){

				indev->LockInputBufferCounter();

				while(indev->inputwritebuffercounter!=indev->inputreadbuffercounter){

					MIDIInBufferEvent *buffer=&indev->buffer[indev->inputreadbuffercounter];

					indev->UnLockInputBufferCounter();

					// SysEx Data ?
					UBYTE *data=buffer->data;
					int datalength=buffer->length;

					buffer->data=0; // Reset
					buffer->length=0;

					//	TRACE ("MIDI Input %d\n",(int)songposition);

					newMIDI.songstatusatinput=asong->status;

					/*
					if(asong->status&Seq_Song::STATUS_SONGPLAYBACK_MIDI)
					newMIDI.netime=asong->timetrack.ConvertTempoTicksToTicks(asong->songstartposition,songposition);
					else
					newMIDI.netime=asong->GetSongPosition();
					*/

					newMIDI.netime=asong->ConvertSysTimeToSongPosition(newMIDI.nesystime=buffer->systime); // Init Song Position
					newMIDI.data=data;
					newMIDI.datalength=datalength;
					newMIDI.status=buffer->status;
					newMIDI.byte1=buffer->bytes[0];
					newMIDI.byte2=buffer->bytes[1];
					newMIDI.fromdev=indev;

					asong->NewMIDIInput(&newMIDI);

					indev->LockInputBufferCounter();

					if(indev->inputreadbuffercounter==MIDIINPUTBUFFER-1)
						indev->inputreadbuffercounter=0;
					else
						indev->inputreadbuffercounter++;

				} // while counter!=

				indev->UnLockInputBufferCounter();

				indev=indev->NextInputDevice();
			}
		}// asong ?
		else
		{
			// Reset Input Counter
			MIDIInputDevice *indev=mainMIDI->FirstMIDIInputDevice();

			while(indev){
				indev->LockInputBufferCounter();
				indev->inputreadbuffercounter=indev->inputwritebuffercounter;
				indev->UnLockInputBufferCounter();

				indev=indev->NextInputDevice();
			}
		}

		Unlock();

	}// while exit

	ThreadGone();
}

PTHREAD_START_ROUTINE MIDIInProc::MIDIInThreadFunc (LPVOID pParam)
{
	//MIDIInProc *inproc=(MIDIInProc *)pParam;
	MIDIinproc->WaitLoop();
	return 0;
}

int MIDIInProc::StartThread()
{
	int error=0;

#ifdef WIN32
	ThreadHandle=CreateThread(NULL, 0,(LPTHREAD_START_ROUTINE)MIDIInThreadFunc,(LPVOID)this, 0,0);
	if(ThreadHandle)
		SetThreadPriority(ThreadHandle,THREAD_PRIORITY_ABOVE_NORMAL);

	//SetThreadPriority(ThreadHandle,THREAD_PRIORITY_TIME_CRITICAL); // Best Priority
	//else
	//	error=1;
#endif

	return error;
}

int PluginMIDIInputProc::StartThread()
{
	int error=0;

#ifdef WIN32
	ThreadHandle=CreateThread(NULL, 0,(LPTHREAD_START_ROUTINE)PluginInputFunc,(LPVOID)this, 0,0);
	if(ThreadHandle)
		SetThreadPriority(ThreadHandle,THREAD_PRIORITY_ABOVE_NORMAL);

	//SetThreadPriority(ThreadHandle,THREAD_PRIORITY_TIME_CRITICAL); // Best Priority
	//else
	//	error=1;
#endif

	return error;
}


