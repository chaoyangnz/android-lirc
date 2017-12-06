#include <string.h>
#include <jni.h>
#include <android/log.h>
#include <stdio.h>

#include "lirc/config.h"
#include "lirc/daemons/config_file.h"
#include "lirc/daemons/ir_remote.h"
#include "lirc/daemons/transmit.h"

#define SAMPLERATE 48000
#define PI   3.141592654

/* Global variable */
struct ir_remote *devices;
int initialized;



int read_config_file(char * config_file)
{
	if(initialized) return 1;

	FILE *fd;
	fd=fopen(config_file, "r");
	char msg[255];

	if(fd==NULL)
	{
		sprintf(msg, "could not open config file '%s'", config_file);
		ZLOG_INFO(msg);
		return 0;
	}

	devices=read_config(fd, config_file);
	fclose(fd);

	if(devices==(void *) -1)
	{
		ZLOG_INFO("reading of config file failed");
		return 0;
	}
	else
	{
		ZLOG_INFO("config file read");
	}

	if(devices==NULL)
	{
		ZLOG_INFO("config file contains no valid remote control definition");
		return 0;
	}

	initialized = 1;
	return 1;
}

jint Java_lirc_android_Lirc_parse( JNIEnv* env, jobject thiz, jstring filename)
{
	char * config_file = (char *)(*env)->GetStringUTFChars(env, filename, NULL);
	initialized = 0;
	if (!read_config_file(config_file)) return 0;
	else return 1;
}

jobjectArray Java_lirc_android_Lirc_getDeviceList( JNIEnv* env, jobject thiz)
{
	if (!read_config_file(LIRCDCFGFILE)) return NULL;

	jstring str = NULL;
	jclass strCls = (*env)->FindClass(env,"java/lang/String");
	if (strCls == NULL)
	{
		return NULL;
	}

	int i, device_count=0;
	char msg[255];

	struct ir_remote *all;
	all=devices;

	while(all)
	{
		all=all->next;
		device_count++;
	}

	sprintf(msg, "Device count: %d", device_count);
	ZLOG_INFO(msg);

	jobjectArray result =  (*env)->NewObjectArray(env,device_count,strCls,NULL);

	if (result == NULL)
	{
		return NULL;
	}

	all=devices;

	i=0;
	while(all)
	{
		if(all->name==NULL)
			return NULL;

		str = (*env)->NewStringUTF(env,all->name);
		(*env)->SetObjectArrayElement(env,result,i++,str);
		(*env)->DeleteLocalRef(env,str);

		all=all->next;
	}


	return result;
}

jobjectArray Java_lirc_android_Lirc_getCommandList( JNIEnv* env, jobject thiz, jstring dev)
{
	char * remote_name = (char *)(*env)->GetStringUTFChars(env, dev, NULL);
	struct ir_remote * remote = get_ir_remote(devices, remote_name);

	jstring str = NULL;
	jclass strCls = (*env)->FindClass(env,"java/lang/String");
	if (strCls == NULL)
	{
		return NULL;
	}

	int i, command_count=0;
	char msg[255];

	struct ir_ncode *all;
	all=remote->codes;

	while(all->name!=NULL)
	{
		command_count++;
		all++;
	}

	sprintf(msg, "Command count: %d", command_count);
	ZLOG_INFO(msg);

	jobjectArray result =  (*env)->NewObjectArray(env,command_count,strCls,NULL);
	if (result == NULL)
	{
		return NULL;
	}

	for(i=0;i<command_count;i++)
	{
		str = (*env)->NewStringUTF(env,remote->codes[i].name);
		(*env)->SetObjectArrayElement(env,result,i,str);
		(*env)->DeleteLocalRef(env,str);
	}

	return result;
}




jintArray Java_lirc_android_Lirc_getInfraredBuffer( JNIEnv* env, jobject thiz,
		jstring dev, jstring cmd, jint min_buf_size)
{
/* variables */
	char * remote_name = (char *)(*env)->GetStringUTFChars(env, dev, NULL);
	char * code_name = (char *)(*env)->GetStringUTFChars(env, cmd, NULL);

	int     length;
	lirc_t* signals;
	jbyteArray result;

	lirc_t  freq;
	char msg[255];


/* Check config */
	if(!read_config_file(LIRCDCFGFILE)) return 0;


/* Prepare send - using lirc/daemons/transmit.h*/
	struct ir_remote * remote = get_ir_remote(devices, remote_name);
	struct ir_ncode * code = get_code_by_name(remote, code_name);

	if(!init_send(remote, code))
		return NULL;

	length = send_buffer.wptr;
	signals = send_buffer.data;

	if (length <= 0 || signals == NULL)
	{
		ZLOG_INFO("nothing to send");
		return NULL;
	}

	/*carrier frequency */
	freq = remote->freq ? remote->freq : DEFAULT_FREQ;

	sprintf(msg, "Using carrier frequency %i", freq);
	ZLOG_INFO(msg);


/* Generate audio buffer - Derived from lirc/daemons/hw_audio.c */
	int i, j=0, signalPhase=0, carrierPos=0, out;
	int currentSignal=0;
	unsigned char outptr[SAMPLERATE*5];

	/* Insert space before the ir code */
	for(j=0; j<5000; j++)
	{
		outptr[j] = 128;
	}

	for(i = 0; i<length; i++)
	{
		/* invert the phase */
		signalPhase = signalPhase ? 0 : 1;

		for(currentSignal=signals[i]; currentSignal>0;
				currentSignal -= 1000000.0 / SAMPLERATE)
		{
			if (signalPhase)
			{
				/* write carrier */
				out = rint(sin(carrierPos / (180.0 / PI)) * 127.0 + 128.0);
			}
			else
			{
				out = 128;
			}

			/* one channel is inverted, so both channels
			   can be used to double the voltage */
			outptr[j++] = out;
			outptr[j++] = 256 - out;

			/* increase carrier position */
			/* carrier frequency is halved */
			carrierPos += (double)freq / SAMPLERATE * 360.0 / 2.0;

			if (carrierPos >= 360.0)
				carrierPos -= 360.0;
		}
	}

	/* Fill with space if buffer is too small */
	if (j < min_buf_size)
	{
		for (j; j<min_buf_size; j++)
		{
			outptr[j] = 128;
			sprintf(msg, "filling buffer: %d/%d",j, min_buf_size);
				ZLOG_INFO(msg);
		}
	}

	sprintf(msg, "Buffer size: %d bytes (min: %d)",j, min_buf_size);
	ZLOG_INFO(msg);


/* Return audio buffer*/
	result = (*env)->NewByteArray(env, j);
	if (result == NULL)
	{
		return NULL;
	}

	(*env)->SetByteArrayRegion(env, result, 0, j, outptr);
	return result;
}
