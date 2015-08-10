#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "webrtc/modules/audio_device/include/audio_device.h"
#include "webrtc/common_audio/resampler/include/resampler.h"
#include "webrtc/modules/audio_processing/aec/include/echo_cancellation.h"
#include "webrtc/common_audio/vad/include/webrtc_vad.h"
//#include "log.cpp"

#define SAMPLE_RATE			32000
#define SAMPLES_PER_10MS	(SAMPLE_RATE/100)

class ChunkBuffer
{
private:
	int chunk_count;
	int chunk_size;
	int used;
	int first;
	char *buf;

public:
	ChunkBuffer(int chunk_count, int chunk_size){
		this->chunk_count = chunk_count;
		this->chunk_size = chunk_size;
		buf = (char *)malloc(chunk_count * chunk_size);
		used = 0;
		first = 0;
	}

	~ChunkBuffer(){
		free(buf);
	}

	// the user of this method must fill the returned chunk of memory,
	// if buffer is null, NULL is returned.
	void* push(){
		if (used >= chunk_count){
			return NULL;
		}
		int available = (first + used) % chunk_count;
		used++;
		return buf + available * chunk_size;
	}

	void* pop(){
		if (used <= 0){
			return NULL;
		}
		void *ret = buf + first * chunk_size;
		used--;
		first = ++first % chunk_count;
		return ret;
	}
};

class AudioTransportImpl : public webrtc::AudioTransport
{
private:
	webrtc::Resampler *resampler_in;
	webrtc::Resampler *resampler_out;
	ChunkBuffer *buffer;
	void *aec;
	VadInst *vad;

public:
	AudioTransportImpl(webrtc::AudioDeviceModule* audio){
		resampler_in = new webrtc::Resampler(48000, SAMPLE_RATE, webrtc::kResamplerSynchronous);
		resampler_out = new webrtc::Resampler(SAMPLE_RATE, 48000, webrtc::kResamplerSynchronous);
		buffer = new ChunkBuffer(10, SAMPLES_PER_10MS * sizeof(int16_t));
		int ret;

		WebRtcAec_Create(&aec);
		assert(aec);
		ret = WebRtcAec_Init(aec, SAMPLE_RATE, SAMPLE_RATE);
		assert(ret == 0);

		WebRtcVad_Create(&vad);
		assert(vad);
		ret = WebRtcVad_Init(vad);
		assert(ret == 0);
	}

	~AudioTransportImpl(){
		delete resampler_in;
		delete resampler_out;
		delete buffer;
		WebRtcAec_Free(aec);
		WebRtcVad_Free(vad);
	}

	virtual int32_t RecordedDataIsAvailable(
		const void* audioSamples,
		const uint32_t nSamples,
		const uint8_t nBytesPerSample,
		const uint8_t nChannels,
		const uint32_t samplesPerSec,
		const uint32_t totalDelayMS,
		const int32_t clockDrift,
		const uint32_t currentMicLevel,
		const bool keyPressed,
		uint32_t& newMicLevel)
	{
		/*
		log_debug("record %d %d %d %d %d %d %d %d", nSamples,
		nBytesPerSample, nChannels,
		samplesPerSec, totalDelayMS,
		clockDrift, currentMicLevel, keyPressed);
		*/
		int ret;
		ret = WebRtcVad_Process(vad, samplesPerSec,
			(int16_t*)audioSamples, nSamples);
		if (ret == 0){
			return 0;
		}
		//log_debug("");

		int16_t *samples = (int16_t *)buffer->push();
		if (samples == NULL){
			//log_debug("drop oldest recorded frame");
			buffer->pop();
			samples = (int16_t *)buffer->push();
			assert(samples);
		}

		int maxLen = SAMPLES_PER_10MS;
		int outLen = 0;
		resampler_in->Push((const int16_t*)audioSamples,
			nSamples,
			samples,
			maxLen, outLen);

		/*
		//log_debug("record %d bytes", outLen*2);
		ret = WebRtcAec_Process(aec,
		samples,
		NULL,
		samples,
		NULL,
		SAMPLES_PER_10MS,
		totalDelayMS,
		0);
		assert(ret != -1);
		if(ret == -1){
		log_debug("%d %d", ret, WebRtcAec_get_error_code(aec));
		}
		int status = 0;
		int err = WebRtcAec_get_echo_status(aec, &status);
		//log_debug("%d %d", status, err);
		if(status == 1){
		//log_debug("wwwwwwwwwwwwwwwwwwwww");
		}
		*/

		return 0;
	}

	virtual int32_t NeedMorePlayData(	
		const uint32_t nSamples,
		const uint8_t nBytesPerSample,
		const uint8_t nChannels,
		const uint32_t samplesPerSec,
		void* audioSamples,
		uint32_t& nSamplesOut,
		int64_t* elapsed_time_ms,
		int64_t* ntp_time_ms)
	{
		// TODO: multithread lock
		/*
		log_debug("playout %d %d %d %d", nSamples,
		nBytesPerSample, nChannels,
		samplesPerSec);
		*/

		int16_t *samples = (int16_t *)buffer->pop();
		if (samples == NULL){
			//log_debug("no data for playout");
			int16_t *ptr16Out = (int16_t *)audioSamples;
			for (int i = 0; i < nSamples; i++){
				*ptr16Out = 0; // left
				ptr16Out++;
				*ptr16Out = 0; // right (same as left sample)
				ptr16Out++;
			}
			return 0;
		}


		int ret = 0;
		//ret = WebRtcAec_BufferFarend(aec, samples, SAMPLES_PER_10MS);
		//assert(ret == 0);


		int outLen = 0;

		int16_t samplesOut[48000];
		resampler_out->Push(samples,
			SAMPLES_PER_10MS,
			samplesOut,
			nSamples * nBytesPerSample, outLen);

		//log_debug("play %d bytes", outLen * 2);

		int16_t *ptr16Out = (int16_t *)audioSamples;
		int16_t *ptr16In = samplesOut;
		// do mono -> stereo
		for (int i = 0; i < nSamples; i++){
			*ptr16Out = *ptr16In; // left
			ptr16Out++;
			*ptr16Out = *ptr16In; // right (same as left sample)
			ptr16Out++;
			ptr16In++;
		}

		nSamplesOut = nSamples;
		return 0;
	}

	virtual int OnDataAvailable(
		const int voe_channels[],
		int number_of_voe_channels,
		const int16_t* audio_data,
		int sample_rate,
		int number_of_channels,
		int number_of_frames,
		int audio_delay_milliseconds,
		int current_volume,
		bool key_pressed,
		bool need_audio_processing)
	{
		//log_debug("aaaa");
		return 0;
	}

public:
};

int main(int argc, char **argv){
	webrtc::AudioDeviceModule *audio;
	audio = webrtc::CreateAudioDeviceModule(0, webrtc::AudioDeviceModule::kPlatformDefaultAudio);
	assert(audio);
	audio->Init();

	int num;
	int ret;

	num = audio->RecordingDevices();
	printf("Input devices: %d\n", num);
	for (int i = 0; i<num; i++){
		char name[webrtc::kAdmMaxDeviceNameSize];
		char guid[webrtc::kAdmMaxGuidSize];
		int ret = audio->RecordingDeviceName(i, name, guid);
		if (ret != -1){
			printf("	%d %s %s\n", i, name, guid);
		}
	}

	num = audio->PlayoutDevices();
	printf("Output devices: %d\n", num);
	for (int i = 0; i<num; i++){
		char name[webrtc::kAdmMaxDeviceNameSize];
		char guid[webrtc::kAdmMaxGuidSize];
		int ret = audio->PlayoutDeviceName(i, name, guid);
		if (ret != -1){
			printf("	%d %s %s\n", i, name, guid);
		}
	}

	ret = audio->SetPlayoutDevice(0);
	ret = audio->SetPlayoutSampleRate(16000);
	if (ret == -1){
		uint32_t rate = 0;
		audio->PlayoutSampleRate(&rate);
		//log_debug("use resampler for playout, device samplerate: %u", rate);
	}
	ret = audio->InitPlayout();

	ret = audio->SetRecordingDevice(0);
	ret = audio->SetRecordingSampleRate(16000);
	if (ret == -1){
		uint32_t rate = 0;
		audio->RecordingSampleRate(&rate);
		//log_debug("use resampler for recording, device samplerate: %u", rate);
	}
	ret = audio->InitRecording();


	AudioTransportImpl callback(audio);
	ret = audio->RegisterAudioCallback(&callback);

	ret = audio->StartPlayout();
	//log_debug("");
	ret = audio->StartRecording();
	//log_debug("");

	//sleep(2);
	getchar();
	return 0;
}