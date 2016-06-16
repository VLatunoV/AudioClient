#include "socket_layer.h"
#include <iostream>
#include <mmdeviceapi.h> // link Winmm.lib
#include <Audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <thread>

#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }
#define PORT "27000"

using namespace std;
using namespace socketLayer;

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
IAudioRenderClient* render_client = NULL;
IAudioClient* audio_client_render = NULL;
IMMDevice* render_device = NULL;
IPropertyStore* propstore_render = NULL;
PROPVARIANT pv_render;
WAVEFORMATEX* wf_render = NULL;
WAVEFORMATEX partner_format;
Connection ReceivingSocket;

void RenderAudio(void*)
{
	UINT32 buffer_size;
	int frame_size = wf_render->nBlockAlign;
	audio_client_render->GetBufferSize(&buffer_size);
	char* receive_buffer = new char [buffer_size * frame_size * 16]; // Big enough
	char* preproccess_buffer = NULL;
	char* used_buffer = receive_buffer;
	int size;
	int offset = 0;
	UINT32 padding;
	UINT32 frames_available;
	UINT32 request_frames;
	BYTE* audio_buffer;
	bool scale_samples = false;
	float sample_coeff;
	float one_over_sample_coeff;
	/*if (wf_render->nSamplesPerSec != partner_format.nSamplesPerSec)
	{
		sample_coeff = (float)wf_render->nSamplesPerSec / (float)partner_format.nSamplesPerSec;
		one_over_sample_coeff = 1.f / sample_coeff;
		preproccess_buffer = new char[buffer_size * frame_size * 16];
		scale_samples = true;
		used_buffer = preproccess_buffer;
	}
	else
	{
		used_buffer = receive_buffer;
	}*/
	while (true)
	{
		if (!ReceivingSocket.Recv(receive_buffer, buffer_size * frame_size * 16, size))
		{
			break;
		}
		/*if (scale_samples)
		{
			size = size * sample_coeff;
			for (int i = 0; i < size; i += 4)
			{
				((float*)preproccess_buffer)[i] = ((float*)receive_buffer)[(int)(i * one_over_sample_coeff)];
			}
		}*/
		//printf("Received %d bytes\n", size);
		while (size - offset > 0)
		{
			audio_client_render->GetCurrentPadding(&padding);
			frames_available = buffer_size - padding;
			request_frames = frames_available < (size - offset) / frame_size ? frames_available : (size - offset) / frame_size;
			if (!request_frames)
			{
				//printf("[DEBUG] Audio loss: %d\n", size - offset);
				break;
			}
			render_client->GetBuffer(request_frames, &audio_buffer);
			memcpy(audio_buffer, used_buffer + offset, request_frames * frame_size);
			offset += request_frames * frame_size;
			render_client->ReleaseBuffer(request_frames, 0);
			//Sleep(request_frames * wf_render->nBlockAlign * 1000 / wf_render->nAvgBytesPerSec);
			//printf("Cycle: %d, %d, %d\n", size, offset, frames_available);
		}
		offset = 0;
	}
}

int main(int argc, char** argv)
{
	if (!Initialize())
	{
		cout << "Failed to initialize socket layer\n";
		system("pause");
		return -1;
	}
	if (!Bind(PORT))
	{
		cout << "Failed to bind listening socket\n";
		system("pause");
		Cleanup();
		return -1;
	}
	Connection SendingSocket;

	char choice;
	char IP[16];
	cout << "Host or Join (h/j)\n";
	cin >> choice;
	if (choice == 'h')
	{
		if (!ReceivingSocket.Accept())
		{
			cout << "Error while connecting to partner\n";
			system("pause");
			Cleanup();
			return -1;
		}
		if(!SendingSocket.Accept())
		{
			cout << "Error while connecting to partner\n";
			system("pause");
			Cleanup();
			return -1;
		}
	}
	else
	{
		cout << "IP: ";
		cin >> IP;
		if (!SendingSocket.Connect(IP, PORT))
		{
			cout << "Error while connecting to partner\n";
			cout << StringError() << endl;
			system("pause");
			Cleanup();
			return -1;
		}
		if(!ReceivingSocket.Connect(IP, PORT))
		{
			cout << "Error while connecting to partner\n";
			cout << StringError() << endl;
			system("pause");
			Cleanup();
			return -1;
		}
	}

	HRESULT result;
	IAudioCaptureClient* capture_client = NULL;
	IAudioClient* audio_client_capture = NULL;
	IMMDevice* capture_device = NULL;
	IMMDeviceEnumerator* enumerator = NULL;
	IPropertyStore* propstore_capture = NULL;
	PROPVARIANT pv_capture;
	WAVEFORMATEX* wf_capture = NULL;
	UINT32 packSize;
	UINT32 availableFrames;
	UINT32 timeIntervalForBuffer = 1000000;
	UINT32 timeIntervalInMilliseconds = timeIntervalForBuffer / 10000;
	BYTE* pData = NULL;
	DWORD flags;
	float index = 0;

	result = CoInitialize(0);
	if (FAILED(result))
	{
		printf("Failed to initialize\n");
		goto Exit;
	}
	result = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL,
		CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
		(void**)&enumerator);
	if (FAILED(result))
	{
		printf("Failed to create device enumerator\n");
		goto Exit;
	}
	result = enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &capture_device);
	if (FAILED(result))
	{
		printf("Failed to get capture endpoint handle\n");
		goto Exit;
	}
	PropVariantInit(&pv_capture);
	result = capture_device->OpenPropertyStore(STGM_READ, &propstore_capture);
	if (FAILED(result))
	{
		printf("Failed to read device properties\n");
		goto Exit;
	}
	propstore_capture->GetValue(PKEY_Device_FriendlyName, &pv_capture);
	printf("Opening capture device: %S\n", pv_capture.pwszVal);
	PropVariantClear(&pv_capture);
	result = capture_device->Activate(
		__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&audio_client_capture);
	if (FAILED(result))
	{
		printf("Failed to activate capture device\n");
		goto Exit;
	}
	result = audio_client_capture->GetMixFormat(&wf_capture);
	if (FAILED(result))
	{
		printf("Failed to get mix format\n");
		goto Exit;
	}
	result = audio_client_capture->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, timeIntervalForBuffer, 0, wf_capture, NULL);
	if (FAILED(result))
	{
		printf("Failed to initialize audio client\n");
		goto Exit;
	}
	printf("Sample rate: %u Hz\n", wf_capture->nSamplesPerSec);
	printf("Sample size: %u bits\n", wf_capture->wBitsPerSample);
	printf("Size of audio frame: %u bytes\n", wf_capture->nBlockAlign);
	printf("Number of channels: %u\n", wf_capture->nChannels);
	result = audio_client_capture->Start();
	if (FAILED(result))
	{
		printf("Failed to start recording\n");
		goto Exit;
	}
	result = audio_client_capture->GetService(__uuidof(IAudioCaptureClient), (void**)&capture_client);
	if (FAILED(result))
	{
		printf("Failed to get capture service\n");
		goto Exit;
	}
	
	// Get render endpoint interface
	result = enumerator->GetDefaultAudioEndpoint(eRender, eCommunications, &render_device);
	if (FAILED(result))
	{
		printf("Failed to get render endpoint handle\n");
		goto Exit;
	}
	PropVariantInit(&pv_render);
	result = render_device->OpenPropertyStore(STGM_READ, &propstore_render);
	if (FAILED(result))
	{
		printf("Failed to read device properties\n");
		goto Exit;
	}
	propstore_render->GetValue(PKEY_Device_FriendlyName, &pv_render);
	printf("Opening render device: %S\n", pv_render.pwszVal);
	PropVariantClear(&pv_render);
	result = render_device->Activate(
		__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&audio_client_render);
	if (FAILED(result))
	{
		printf("Failed to activate render device\n");
		goto Exit;
	}
	result = audio_client_render->GetMixFormat(&wf_render);
	if (FAILED(result))
	{
		printf("Failed to get mix format\n");
		goto Exit;
	}
	result = audio_client_render->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, timeIntervalForBuffer, 0, wf_render, NULL);
	if (FAILED(result))
	{
		printf("Failed to initialize audio client\n");
		goto Exit;
	}
	printf("Sample rate: %u Hz\n", wf_render->nSamplesPerSec);
	printf("Sample size: %u bits\n", wf_render->wBitsPerSample);
	printf("Size of audio frame: %u bytes\n", wf_render->nBlockAlign);
	printf("Number of channels: %u\n", wf_render->nChannels);
	result = audio_client_render->Start();
	if (FAILED(result))
	{
		printf("Failed to start recording\n");
		goto Exit;
	}
	result = audio_client_render->GetService(__uuidof(IAudioRenderClient), (void**)&render_client);
	if (FAILED(result))
	{
		printf("Failed to get render service\n");
		goto Exit;
	}

	int partner_format_received_size;
	SendingSocket.Send((const char*)wf_capture, sizeof(*wf_capture));
	ReceivingSocket.Recv((char*)&partner_format, sizeof(partner_format), partner_format_received_size);
	if (last_error != _NO_ERROR)
	{
		printf("Connection failed\n");
		goto Exit;
	}
	if (partner_format.nSamplesPerSec != wf_render->nSamplesPerSec)
	{
		/*cout << "Partned format:\n";
		cout << partner_format.wBitsPerSample << endl;
		cout << partner_format.nSamplesPerSec << endl;
		cout << "My format:\n";
		cout << wf_render->wBitsPerSample << endl;
		cout << wf_render->nSamplesPerSec << endl;*/
		printf("Partner capture format unsupported\n");
		//goto Exit;
	}
	LPTHREAD_START_ROUTINE StartRoutine = (LPTHREAD_START_ROUTINE)RenderAudio;
	DWORD threadID;
	HANDLE hThread = CreateThread(NULL, 0, StartRoutine, NULL, 0, &threadID);
	while (true)
	{
		Sleep(timeIntervalInMilliseconds);
		result = capture_client->GetNextPacketSize(&packSize);
		if (FAILED(result))
		{
			printf("Failed to get next pack size\n");
			break;
		}
		while (packSize)
		{
			result = capture_client->GetBuffer(&pData, &availableFrames, &flags, NULL, NULL);
			if (FAILED(result))
			{
				printf("Failed to get buffer\n");
				goto Exit;
			}
			if (!SendingSocket.Send((const char*)pData, availableFrames * wf_capture->nBlockAlign))
			{
				if (last_error == SOCKET_CLOSED)
				{
					printf("Disconnect\n");
				}
				else
				{
					printf("Connection broke\n");
				}
				goto Exit;
			}
			result = capture_client->ReleaseBuffer(packSize);
			result = capture_client->GetNextPacketSize(&packSize);
			if (FAILED(result))
			{
				printf("Failed to release buffer\n");
				goto Exit;
			}
		}
		result = capture_client->ReleaseBuffer(packSize);
		if (GetAsyncKeyState(VK_ESCAPE))
		{
			break;
		}
	}
	
Exit:
	audio_client_capture->Stop();
	//TerminateThread(hThread, 0);
	audio_client_render->Stop();

	SendingSocket.Disconnect();
	ReceivingSocket.Disconnect();
	SAFE_RELEASE(capture_device);
	SAFE_RELEASE(render_device);
	SAFE_RELEASE(enumerator);
	SAFE_RELEASE(propstore_capture);
	SAFE_RELEASE(propstore_render);
	SAFE_RELEASE(capture_client);
	SAFE_RELEASE(render_client);
	SAFE_RELEASE(audio_client_capture);
	SAFE_RELEASE(audio_client_render);
	system("pause");
	return 0;
}
