/*
Copyright (c) 2019 KT corp.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
// test_sample.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include  "stdio.h"
#include  "string.h"
#include  "unistd.h"
#include  "sys/time.h"
#include  "time.h"
#include  <mutex>
#include  <list>
#include  <pthread.h>
#include  <sys/types.h>
#include  <sys/ipc.h> 
#include  <sys/msg.h> 
#include  <sys/stat.h> 

#include  "ginside.h"
#include  "ginsidedef.h"
#include  "base64.h"
#include  "cJSON.h"

#include <alsa/asoundlib.h>

#define  SLEEP_MS(x)    usleep((x)*1000)
#define  strcmp         strcasecmp

#define  THREAD_RET_TYPE            void *
#define  THREAD_CALLING_CONVENTION
#define  THREAD_RETURN              return NULL
#define  THREAD_HANDLE              pthread_t

THREAD_HANDLE SpawnThread(THREAD_RET_TYPE(THREAD_CALLING_CONVENTION *func)(void *), void *arg);

THREAD_RET_TYPE THREAD_CALLING_CONVENTION TimerThread(void *arg);
THREAD_RET_TYPE THREAD_CALLING_CONVENTION MicThread(void *arg);
THREAD_RET_TYPE THREAD_CALLING_CONVENTION OnCommandThread(void *arg);
THREAD_RET_TYPE THREAD_CALLING_CONVENTION KwsThread(void *arg);

int g_start_kws = 1;
int g_start_mic = 0;

bool isInit = false;
bool isKwsInit = false;

std::string g_clientId = "";
std::string g_clientKey = "";
std::string g_clientSecret = "";
std::string g_uuid = "";

// 알람/타이머 정보 관리를 위한 구조체
typedef struct _AlarmTimerInfo {
        std::string reqAct;
        std::string actTrx;
        std::string localTime;
} AlarmTimerInfo;
std::list<AlarmTimerInfo> g_alarmtimerList;
std::mutex g_alarm_mtx;

// onCommand로 전달되는 메시지 관리를 위한 구조체
typedef struct _oncmd_msg_t {
        std::string actionType;
        std::string payload;
} oncmd_msg_t;
std::list<oncmd_msg_t> g_onCmdList;
std::mutex oncmd_mtx;

//VLCPlayer player;
unsigned int prevCh = 100;
unsigned int curCh = 100;

void onEvent(int eventMask, std::string opt);
void onCommand(std::string actionType, std::string payload);

THREAD_HANDLE SpawnThread(THREAD_RET_TYPE(THREAD_CALLING_CONVENTION *func)(void *), void *arg)
{
    THREAD_HANDLE   handle;

    if (pthread_create(&handle, NULL, func, arg) != 0)
    {
        perror("pthread_create returned an error!");
    }

    return handle;
}

void getfilename(char *pfilename)
{
    struct timeval      tv;
    struct timezone     tz;
    struct tm           *tptr, t;

    gettimeofday(&tv, &tz);
    localtime_r(&tv.tv_sec, &t);
    tptr = &t;

    sprintf(pfilename, "%02d%02d_%02d%02d%02d",
        tptr->tm_mon + 1,
        tptr->tm_mday,
        tptr->tm_hour,
        tptr->tm_min,
        tptr->tm_sec);
}

// TODO: 설정된 알람/타이머 시간이 도래되면 해당 알람/타이머 정보(reqAct, actTrx, localTime)를 SDK로 전달한다.
void sendTimeEvent(const char *pReqAct, const char *pActionTrx, const char *pLocalTime)
{
    cJSON *payload_root = cJSON_CreateObject();
    cJSON *cmdtype = cJSON_CreateString(CMD_SND_TMEV);
    cJSON *cmdOpt_reqAct = cJSON_CreateString(pReqAct);
    cJSON *cmdOpt_actionTrx = cJSON_CreateString(pActionTrx);
    cJSON *cmdOpt_localTime = cJSON_CreateString(pLocalTime);

    cJSON_AddItemToObject(payload_root, "cmdType", cmdtype);
    cJSON_AddItemToObject(payload_root, "reqAct", cmdOpt_reqAct);
    cJSON_AddItemToObject(payload_root, "actionTrx", cmdOpt_actionTrx);
    cJSON_AddItemToObject(payload_root, "localTime", cmdOpt_localTime);

    char *msgPayloadStr = cJSON_Print(payload_root);
    printf("sendTimeEvent:\n%s\n", msgPayloadStr);
    agent_sendCommand(msgPayloadStr);

    cJSON_Delete(payload_root);
    free(msgPayloadStr);
}

// TODO: 볼륨이 변경되거나 BT 이벤트 발생 시 SDK로 해당 이벤트를 전달한다.
// target : "volume"(볼륨), "bluetooth"(BT)
// event : "setVlume"(볼륨 변경), "BTEvent"(BT 이벤트)
// opt : 볼륨({ "value": #}), BT({"event": 800/801/802})
void sendHwEvent(std::string target, std::string event, std::string opt)
{
    cJSON *payload_root = cJSON_CreateObject();

    cJSON *cmdtype = cJSON_CreateString(CMD_SND_HWEV);
    cJSON *cmdOpt_target = cJSON_CreateString((const char*)target.c_str());
    cJSON *cmdOpt_event = cJSON_CreateString((const char*)event.c_str());
    cJSON *cmdOpt_opt = cJSON_CreateString((const char*)opt.c_str());

    cJSON_AddItemToObject(payload_root, "cmdType", cmdtype);
    cJSON_AddItemToObject(payload_root, "target", cmdOpt_target);
    cJSON_AddItemToObject(payload_root, "event", cmdOpt_event);
    cJSON_AddItemToObject(payload_root, "eventOpt", cmdOpt_opt);
    if (target == "volume")
    {
        cJSON *cmdOpt_volume = cJSON_CreateNumber(atoi(opt.c_str()));
        cJSON_AddItemToObject(cmdOpt_opt, "value", cmdOpt_volume);
    }
    char *msgPayloadStr = cJSON_Print(payload_root);
    printf("sendHwEvent:\n%s\n", msgPayloadStr);
    agent_sendCommand(msgPayloadStr);

    cJSON_Delete(payload_root);
    free(msgPayloadStr);
}

// TODO: "control_hardware"를 통해 수신된 제어명령을 처리한 후 그 결과를 SDK로 전달한다.
// target : control_hardware payload의 target
// hwCmd : control_hardware payload의 hwCmd
// hwCmdResult : 처리 결과 메시지 (임의의 메시지를 전달하면 된다.)
// trxid : control_hardware payload의 trxid
void sendResHwcl(const char *pTarget, const char *pHwCmd, const char *pHwCmdResult, const char *pTrxid) 
{
    cJSON *payload_root = cJSON_CreateObject();            
    cJSON *cmdtype = cJSON_CreateString(CMD_RES_HWCL);
    cJSON *cmdOpt_target = cJSON_CreateString(pTarget);
    cJSON *cmdOpt_hwcmd = cJSON_CreateString(pHwCmd);
    cJSON *cmdOpt_hwcmdResult = cJSON_CreateString(pHwCmdResult);
    cJSON *cmdOpt_trxid = cJSON_CreateString(pTrxid);
        
    cJSON_AddItemToObject(payload_root, "cmdType", cmdtype);
    cJSON_AddItemToObject(payload_root, "target", cmdOpt_target);
    cJSON_AddItemToObject(payload_root, "hwCmd", cmdOpt_hwcmd);
    cJSON_AddItemToObject(payload_root, "hwCmdResult", cmdOpt_hwcmdResult);
    cJSON_AddItemToObject(payload_root, "trxid", cmdOpt_trxid);
        
    char *msgPayloadStr = cJSON_Print(payload_root);
    printf("sendResHwcl:\n%s\n", msgPayloadStr);
    agent_sendCommand(msgPayloadStr);

    free(msgPayloadStr);
    cJSON_Delete(payload_root);    
}

// TODO : 알람/타이머 설정/해제 처리를 수행한다.
void processReqSTTM(std::string cmdPayload)
{
    printf("%s\n", __FUNCTION__);

    cJSON *cmdp_jsonObj = cJSON_Parse(cmdPayload.c_str());
    if (cmdp_jsonObj == NULL)
    {
        printf("cjson parsing Error");
    }
    else
    {
        cJSON *cmdp_payload = cJSON_GetObjectItem(cmdp_jsonObj, "payload");
        if (cmdp_payload != NULL)
        {
            cJSON *cmdp_cmdOpt = cJSON_GetObjectItem(cmdp_payload, "cmdOpt");
            if (cmdp_cmdOpt != NULL)
            {
                // TODO : 서버에서 전달 받은 타이머 정보를 내부에 저장하고, (설정한 타이머/알람 개수만큼 전달되므로 수신한 타이머/알람 정보는 리스트 형태로 관리되어야 한다.
                // TODO : 타이머 시간이 도래하면 SND_TMEV 메시지를 SDK에게 전달한다. (문서 상 Snd_TMEV 를 참조)
                cJSON *setOpt_obj = cJSON_GetObjectItem(cmdp_cmdOpt, "setOpt");
                cJSON *reqAct_obj = cJSON_GetObjectItem(cmdp_cmdOpt, "reqAct");
                cJSON *actionTrx_obj = cJSON_GetObjectItem(cmdp_cmdOpt, "actionTrx");
                cJSON *setTime_obj = cJSON_GetObjectItem(cmdp_cmdOpt, "setTime");
                if (setOpt_obj != NULL && reqAct_obj != NULL && actionTrx_obj != NULL && setTime_obj != NULL) 
                {
                    std::string setOpt(setOpt_obj->valuestring);
                    std::string reqAct(reqAct_obj->valuestring);
                    std::string actionTrx(actionTrx_obj->valuestring);
                    std::string setTime(setTime_obj->valuestring);
                    printf("setOpt=%s, reqAct=%s, actionTrx=%s, setTime=%s\n", setOpt.c_str(), reqAct.c_str(), actionTrx.c_str(), setTime.c_str());
                    
                    if(setOpt == "set") {
                        // TODO : 알람/타이머 정보 추가
                        // a. 알람/타이머 설정의 경우 기존에 동일한 정보가 있는지 확인한다. (actionTrx 동일 여부 체크)
                        bool found = false;
                        AlarmTimerInfo alarmTimer;
                        g_alarm_mtx.lock();
                        for(std::list<AlarmTimerInfo>::iterator iter = g_alarmtimerList.begin(); iter != g_alarmtimerList.end(); iter++) {
                            alarmTimer = *iter;
                            if(alarmTimer.actTrx == actionTrx) {
                                found = true;
                                break;
                            }
                        }
                        g_alarm_mtx.unlock();
                        
                        // b. 기존에 설정된 알람/타이머 정보가 없는 경우 저장/관리 한다.
                        if(!found) {
                            // c. setTime 정보가 "000000"으로 전달된 경우 현재 시간으로 설정한다.                        
                            if(setTime.find("000000") == 0) {
                                time_t timeSet = time(NULL) + atoi(setTime.c_str());
                                struct tm* time_info = localtime(&timeSet);
                                char *buffer = new char[20];
                                strftime(buffer, 20, "%Y%m%d%H%M%S", time_info);   
                                std::string timeToSet(buffer);
                                delete[] buffer;
                                
                                g_alarm_mtx.lock();
                                g_alarmtimerList.push_back({reqAct, actionTrx, timeToSet});
                                g_alarm_mtx.unlock();
                            } else {
                                g_alarm_mtx.lock();
                                g_alarmtimerList.push_back({reqAct, actionTrx, setTime});
                                g_alarm_mtx.unlock();
                            }
                        }
                    } else if(setOpt == "clear") {
                        // TODO : 알람/타이머 정보 삭제
                        AlarmTimerInfo alarmTimer;
                        g_alarm_mtx.lock();
                        for(std::list<AlarmTimerInfo>::iterator iter = g_alarmtimerList.begin(); iter != g_alarmtimerList.end(); /*iter++*/) {
                            alarmTimer = *iter;
                            if(alarmTimer.actTrx == actionTrx) {
                                iter = g_alarmtimerList.erase(iter);
                            }
                            iter++;
                        }
                        g_alarm_mtx.unlock();
                    }
                }
            }
        }
        cJSON_Delete(cmdp_jsonObj);
    }
}

// 알람/타이머 체크를 위한 Thread
THREAD_RET_TYPE THREAD_CALLING_CONVENTION TimerThread(void *arg)
{
    time_t curtime_t;
    struct tm* time_info;
    AlarmTimerInfo alarmTimer;
    int listSize = 0;

    while (true)
    {
        g_alarm_mtx.lock();
        listSize = g_alarmtimerList.size();
        g_alarm_mtx.unlock();

        if(listSize > 0) {
            // TODO : 현재 시간 확인하여 설정된 알람/타이머 시간과 동일한 경우 타이머 이벤트를 전달하거나 내장 알람/타이머 음원을 재생한다.
            curtime_t = time(NULL);
            time_info = localtime(&curtime_t);        
            char *buffer = new char[20];
            strftime(buffer, 20, "%Y%m%d%H%M%S", time_info);
            std::string curTime(buffer);
            delete[] buffer;
        
            g_alarm_mtx.lock();
            for(std::list<AlarmTimerInfo>::iterator iter = g_alarmtimerList.begin(); iter != g_alarmtimerList.end(); iter++) {
                alarmTimer = *iter;
                if(alarmTimer.localTime == curTime) {
                    // TODO : 타이머 이벤트를 SDK로 전달한다. (네트웍 연결이 안된 경우에는 자체적으로 내장 음원을 재생할 수 있다.)
                       /*
                    if(is_network_connected) {
                        sendTimeEvent(alarmTimer.reqAct.c_str(), alarmTimer.actTrx.c_str(), alarmTimer.localTime.c_str());
                    } else {
                        if(alarmTimer.reqAct == "0" || alarmTimer.reqAct == "1" || alarmTimer.reqAct == "2" || alarmTimer.reqAct == "3" || alarmTimer.reqAct == "4") {
                            // a. 재생 중인 TTS 종료
                            // b. 알람/타이머 음원(reqAct) 정보에 따른 내장 알람/타이머 재생
                            }
                       }
                       */
                  }
              }
            g_alarm_mtx.unlock();
        }
        SLEEP_MS(1000); // 1 sec sleep
    }

    THREAD_RETURN;
}

// 호출어 인식을 위한 Thread
THREAD_RET_TYPE THREAD_CALLING_CONVENTION KwsThread(void *arg)
{
    char *buffer;
    int err = 0;
    int buffer_frames = 160; //frame size , 1/100 sec 
    unsigned int rate = 16000;//16K;
    snd_pcm_t *capture_handle;
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE; //16bit Little endian
    int rc;

    short test[320];
    int i=0, j=0;
    int chunk_size = 0;
    int ret = 0;
    
    printf("> KWS SDK version: %s, current KWS: %d\n", kws_getVersion(), kws_getKeyword());

    // TODO : 16kHz 16-bit Linear PCM 데이터를 이용하여 호출어 인식을 시도한다.
    while (true)
    {
        if(g_start_kws) {
            if ((err = snd_pcm_open (&capture_handle, "default", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
                fprintf (stderr, "cannot open audio device default (%s)\n",snd_strerror (err));
                exit (1);
            }
            if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
                fprintf (stderr, "cannot allocate hardware parameter structure (%s)\n",snd_strerror (err));
                exit (1);
            }
            if ((err = snd_pcm_hw_params_any (capture_handle, hw_params)) < 0) {
                fprintf (stderr, "cannot initialize hardware parameter structure (%s)\n",snd_strerror (err));
                exit (1);
            }
            if ((err = snd_pcm_hw_params_set_access (capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
                fprintf (stderr, "cannot set access type (%s)\n",snd_strerror (err));
                exit (1);
            }
            if ((err = snd_pcm_hw_params_set_format (capture_handle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0) {
                fprintf (stderr, "cannot set sample forplayermat (%s)\n",snd_strerror (err));
                exit (1);
            }
            if ((err = snd_pcm_hw_params_set_rate_near (capture_handle, hw_params, &rate, 0)) < 0) {
                fprintf (stderr, "cannot set sample rate (%s)\n",snd_strerror (err));
                exit (1);
            }
            if ((err = snd_pcm_hw_params_set_channels (capture_handle, hw_params, 1)) < 0) {
                fprintf (stderr, "cannot set channel count (%s)\n",snd_strerror (err));
                exit (1);
            }
            if ((err = snd_pcm_hw_params (capture_handle, hw_params)) < 0) {
                fprintf (stderr, "cannot set parameters (%s)\n",snd_strerror (err));
                exit (1);
            }
    
            snd_pcm_hw_params_free (hw_params);
    
            if ((err = snd_pcm_prepare (capture_handle)) < 0) {
                fprintf (stderr, "cannot prepare audio interface for use (%s)\n", snd_strerror (err));
                exit (1);
            }
    
            chunk_size = buffer_frames*snd_pcm_format_width(format)/8;
            buffer = (char*)malloc(chunk_size); //320byte
            fprintf(stdout, "KwsThread: chunk_size=%d, buffer_frames=%d\n", chunk_size, buffer_frames);
                
            fprintf(stdout, "> ready to kws_detect()\n");
            while(ret != KWS_DET_DETECTED && g_start_kws) {
                rc = snd_pcm_readi(capture_handle, buffer, buffer_frames);
                if (rc == -EPIPE) {
                    fprintf(stderr, "overrun occurred\n");
                    snd_pcm_prepare(capture_handle);
                } else if (rc < 0) {
                    fprintf(stderr,"error from read: %s\n", snd_strerror(rc));
                } else if (rc != (int)buffer_frames) {
                    fprintf(stderr, "short read, read %d frames\n", rc);
                } else {
                    // TODO : char형 데이터를 short형 데이터로 변환한다.
                    memset(test, 0, 320);
                    j = 0;
                    for(i=0 ; i<chunk_size ; i=i+2 ) {
                        test[j] = (short)(((buffer[i+1] & 0xff) << 8) | (buffer[i] & 0xff));
                        j++;
                    }
                    // test : short형 PCM 데이터
                    // buffer_frames : PCM 입력 sample 개수
                    ret = kws_detect(test, buffer_frames);
                    if(ret == KWS_DET_DETECTED) {
                        printf("> kws detected !!!\n");
                        // TODO : agent_init()을 통해 uuid 정보를 정상적으로 발급받은 상태에서 호출어 인식 후 음성인식 절차를 진행할 수 있다.     
                        if(!isInit) {
                            printf("can't start voice recognition. call agent_init first!\n");
                        } else {
                            // a. 호출어가 인식되면 재생 중인 미디어를 종료 또는 일시정지 한 후 필요에 따라 호출어 인식을 알리는 음원 등을 재생한다.
                            // b. 호출어 인식 알림음을 재생하는 경우에는 알림음 재생이 완료된 후 agent_stratVoice()를 호출하여 음성인식 시작을 요청한다.
                            g_start_kws = 0;
                            agent_startVoice();
                        }
                    }
                }
            }
            free(buffer);
            snd_pcm_close(capture_handle);
            printf("kws detection completed.\n");
            ret = 0;
        }
        SLEEP_MS(10); // 10 msec sleep
    }

    THREAD_RETURN;
}

// 음성인식 처리를 위한 Thread
THREAD_RET_TYPE THREAD_CALLING_CONVENTION MicThread(void *arg)
{
    char *buffer;
    int err = 0;
    int buffer_frames = 160; //frame size , 1/100 sec 
    unsigned int rate = 16000;//16K;
    snd_pcm_t *capture_handle;
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE; //16bit Little endian
    int rc;

    short test[320];
    int i=0, j=0;
    int chunk_size = 0;
    
    while (true)
    {
        // Wait for an event.
        while (g_start_mic == 0)
        {
            SLEEP_MS(10);   // 10 msec sleep
        }

        // TODO : 16kHz 16-bit Linear PCM 데이터를 이용하여 음성인식을 시도한다.
        if ((err = snd_pcm_open (&capture_handle, "default", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
            fprintf (stderr, "cannot open audio device default (%s)\n",snd_strerror (err));
            exit (1);
        }
        if ((err = snd_pcm_hw_params_malloc (&hw_params)) < 0) {
            fprintf (stderr, "cannot allocate hardware parameter structure (%s)\n",snd_strerror (err));
            exit (1);
        }
        if ((err = snd_pcm_hw_params_any (capture_handle, hw_params)) < 0) {
            fprintf (stderr, "cannot initialize hardware parameter structure (%s)\n",snd_strerror (err));
            exit (1);
        }
        if ((err = snd_pcm_hw_params_set_access (capture_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
            fprintf (stderr, "cannot set access type (%s)\n",snd_strerror (err));
            exit (1);
        }
        if ((err = snd_pcm_hw_params_set_format (capture_handle, hw_params, SND_PCM_FORMAT_S16_LE)) < 0) {
            fprintf (stderr, "cannot set sample forplayermat (%s)\n",snd_strerror (err));
            exit (1);
        }
        if ((err = snd_pcm_hw_params_set_rate_near (capture_handle, hw_params, &rate, 0)) < 0) {
            fprintf (stderr, "cannot set sample rate (%s)\n",snd_strerror (err));
            exit (1);
        }
        if ((err = snd_pcm_hw_params_set_channels (capture_handle, hw_params, 1)) < 0) {
            fprintf (stderr, "cannot set channel count (%s)\n",snd_strerror (err));
            exit (1);
        }
        if ((err = snd_pcm_hw_params (capture_handle, hw_params)) < 0) {
            fprintf (stderr, "cannot set parameters (%s)\n",snd_strerror (err));
            exit (1);
        }

        snd_pcm_hw_params_free (hw_params);

        if ((err = snd_pcm_prepare (capture_handle)) < 0) {
            fprintf (stderr, "cannot prepare audio interface for use (%s)\n", snd_strerror (err));
            exit (1);
        }

        chunk_size = buffer_frames*snd_pcm_format_width(format)/8;
        buffer = (char*)malloc(chunk_size); //320byte
        fprintf(stdout, "MicThread: chunk_size=%d, buffer_frames=%d\n", chunk_size, buffer_frames);
        
        fprintf(stdout, "> ready to agent_sendVoice()\n");
        while(g_start_mic){
            rc = snd_pcm_readi(capture_handle, buffer, buffer_frames);
            if (rc == -EPIPE) {
                fprintf(stderr, "overrun occurred\n");
                snd_pcm_prepare(capture_handle);
            } else if (rc < 0) {
                fprintf(stderr,"error from read: %s\n", snd_strerror(rc));
            } else if (rc != (int)buffer_frames) {
                fprintf(stderr, "short read, read %d frames\n", rc);
            } else {
                // TODO : char형 데이터를 short형 데이터로 변환한다.
                memset(test, 0, 320);
                j = 0;
                for(i=0 ; i<chunk_size ; i=i+2 ) {
                    test[j] = (short)(((buffer[i+1] & 0xff) << 8) | (buffer[i] & 0xff));
                    j++;
                }
                // test : short형 PCM 데이터
                // buffer_frames : PCM 입력 sample 개수
                // TODO : onCommand로 actionType="stop_voice"를 수신하기 전까지 음성데이터를 계속 전송한다. 
                agent_sendVoice(test, buffer_frames);
                printf(".");
            }
        }
        free(buffer);
        snd_pcm_close(capture_handle);
        printf("agent_sendVoice() completed.\n");
    }

    THREAD_RETURN;
}

// onCommand callback을 통해 전달되는 메시지를 처리하기 위한 Thread
THREAD_RET_TYPE THREAD_CALLING_CONVENTION OnCommandThread(void *arg)
{
    int ttsDataSize = 0;
    const char* ttsData;
    static char filename[255], filename_ext[255], file_path[255];
    static FILE *fp = NULL;

    int listSize = 0;
    oncmd_msg_t oncmd;
    std::string actionType;
    std::string payload;
    
    while (true)
    {
        oncmd_mtx.lock();
        listSize = g_onCmdList.size();
        oncmd_mtx.unlock();
        
        if(listSize > 0) {
            oncmd_mtx.lock();
            oncmd = g_onCmdList.front();
            oncmd_mtx.unlock();
            
            actionType = oncmd.actionType;
            payload = oncmd.payload;
            if (strcmp(actionType.c_str(), "media_data") == 0) {
                printf("OnCommandThread: actionType=%s\n", actionType.c_str());
            } else {
                printf("OnCommandThread: actionType=%s, payload=%s\n", actionType.c_str(), payload.c_str());                
            }
            if (strcmp(actionType.c_str(), "media_data") == 0) {
                cJSON *cmdp_payload = cJSON_Parse(payload.c_str());
                if (cmdp_payload == NULL) {
                    printf("media_data null.\n");
                } else {
                    cJSON *cmdp_contentType = cJSON_GetObjectItem(cmdp_payload, "contentType");
                    cJSON *cmdp_mediaStream = cJSON_GetObjectItem(cmdp_payload, "mediastream");
                    if(cmdp_contentType != NULL && cmdp_mediaStream != NULL) {
                        std::string contentType(cmdp_contentType->valuestring);
                        std::string media(cmdp_mediaStream->valuestring);

                        // TODO : 수신된 tts data는 디코딩하여 재생해야 한다.
                        std::string decoded = base64_decode(media);
                        printf("media_data size=[%d]\n", decoded.size());
                        ttsDataSize = (int)decoded.size();
                        ttsData = decoded.c_str();
                
                        // TODO : 디코딩된 tts data를 파일로 저장하여 재생하거나 파일 자체를 재생한다.
                        getfilename(filename);
                        sprintf(filename_ext, "%s.%s", filename, contentType.c_str());                
                        printf("Writing data to %s with a size=%d\n", filename_ext, ttsDataSize);                
                        fp = fopen(filename_ext, "w+b");
                        if (fp != NULL) {
                            fwrite(ttsData, 1, ttsDataSize, fp);
                            fclose(fp);
                            fp = NULL;
                        }
                        
                        // TODO : TTS(wav)를 재생하고 재생 시작/완료 이벤트를 Player로부터 수신하면 미디어 재생상태("started"/"completed")를 SDK로 전달해야 한다.
                        // TODO : 미디어 재생 시 actionType="play_media"로 전달된 channel 정보를 관리해야 하며 channel별로 미디어 재생상태를 제어하고 재생 이벤트를 전달할 수 있어야 한다.
                            /*
                        sprintf(file_path, "./%s", filename_ext);
                        std::string url(file_path);                        
                        if(player.play(1, url) == 0) {
                            agent_updateMediaStatus(curCh, "started", 0);
                            }
                            */
                        sprintf(file_path, "aplay ./%s", filename_ext);
                        system(file_path);
                        agent_updateMediaStatus(curCh, "started", 0);

                        memset(filename, 0x0, sizeof(filename));
                        memset(filename_ext, 0x0, sizeof(filename_ext));
                        memset(file_path, 0x0, sizeof(file_path));                          
                    }
                }
                cJSON_Delete(cmdp_payload);
            }
            else if (strcmp(actionType.c_str(), "start_voice") == 0)
            {
                // agent_startVoice()에 대한 응답 또는 연속 대화가 필요한 경우 서버에서 전달되는 메시지
                // TODO : 호출어 인식 중인 경우 호출어 인식을 중단하고 미디어 재생 중인 경우 PAUSE/STOP 처리 후 음성인식을 시작해야 한다. 
                  /*
                if(curCh != 100) {
                    if(curCh >= 0 && curCh < 10) { // TTS 재생 종료
                        player.stop();
                        agent_updateMediaStatus(curCh, "stopped", 재생시간);
                        curCh = 100;
                    } else { // TTS 이외 미디어 pause
                        player.pause();
                        agent_updateMediaStatus(curCh, "paused", 재생시간);
                       }
                  }
                  */
                g_start_mic = 1;
            }
            else if(strcmp(actionType.c_str(), "stop_voice") == 0)
            {
                // agent_sendVoice() 후 음성인식이 완료되면 전달되는 메시지
                // TODO : 음성인식을 중단하고 play_media/control_media/exec_dialogkit/control_hardware 등의 메시지가 수신될 때까지 호출어 인식을 시작하지 않고 기다린다. 
                // TODO : 네트웍 단절이나 서버 이상 등으로 위에서 언급한 메시지가 수신되지 않을 경우를 대비해 내부적으로 자체 타이머 등의 예외처리가 필요하다.
                // e.g.) "stop_voice" 수신 후 10초 타이머를 시작하고, 10초 타이머가 만료될때까지 서버로부터 추가 메시지/이벤트가 전달되지 않으면 에러로 인식하여 사용자에게 안내하고 호출어 인식을 시작한다.
                if (g_start_mic == 1) {
                    printf("Stop to record a voice!\n");
                    g_start_mic = 0;
                } else {
                    printf("Voice Recognition is already stopped!\n");
                }
            }
            else if (strcmp(actionType.c_str(), "set_timer") == 0)
            {
                // TODO : 알람/타이머 설정 시 전달되는 메시지
                // TODO : 수신된 알람/타이머 정보를 내부적으로 관리하고 설정된 알람/타이머 시간 도래 시 타이머 이벤트(sendTimeEvent)를 SDK로 전달한다. 
                processReqSTTM(payload);
                if(g_start_kws == 0) {
                    g_start_kws = 1;
                }
            }
            else if (strcmp(actionType.c_str(), "play_media") == 0)
            {
                // TTS나 음악/라디오 등을 재생하기 위해 전달되는 메시지
                cJSON *cmdp_payload = cJSON_Parse(payload.c_str());
                if (cmdp_payload == NULL)
                {
                    printf("play_media parsing Error.\n");
                }
                else
                {
                    cJSON *cmdp_cmdOpt = cJSON_GetObjectItem(cmdp_payload, "cmdOpt");
                    if (cmdp_cmdOpt != NULL)
                    {
                        // TODO : 채널 별로 미디어 재생상태를 관리하고 상태 변경 시 SDK로 상태를 전달(agent_updateMediaStatus)해야한다.  
                        // channel : 재생해야 할 채널(stream ID)
                        cJSON *cmdp_channel = cJSON_GetObjectItem(cmdp_cmdOpt, "channel");
                        // actOnOther : 기존에 재생 중인 미디어에 대한 제어 명령(pause/pauseR/stop)
                        cJSON *cmdp_actOnOther = cJSON_GetObjectItem(cmdp_cmdOpt, "actOnOther");
                        // url : 재생 할 url (url 정보가 없는 경우 "media_data"로 TTS 데이터가 전달된다.)
                        cJSON *cmdp_url = cJSON_GetObjectItem(cmdp_cmdOpt, "url");
                        if(cmdp_actOnOther != NULL){
                            std::string actonother(cmdp_actOnOther->valuestring);
                            if((actonother == "pause" || actonother == "pauseR") && curCh != 100) {
                                // TODO: 기존에 재생 중인 미디어를 pause 시키고, url 정보가 전달된 경우 url을 재생하거나 url 정보가 없는 경우 뒤에 전달되는 "media_data"를 통해 수신되는 TTS를 재생한다.
                                // "pauseR"의 경우 전달된 channel에 해당하는 미디어 재생이 완료된 후 pause했던 미디어를 다시 resume 시켜야 한다.
                                    /*
                                if(player.pause() == 0) {
                                    agent_updateMediaStatus(curCh, "paused", 재생시간);
                                    prevCh = curCh;
                                    }
                                    */
                            } else if(actonother == "stop" && curCh != 100) {
                                // TODO: 기존에 재생 중인 미디어를 종료 시키고, url이 전달된 경우 url을 재생하거나 url 정보가 없는 경우 뒤에 전달되는 "media_data"를 통해 수신되는 TTS를 재생한다.
                                    /*
                                if(player.stop() == 0) {
                                    agent_updateMediaStatus(curCh, "stopped", 재생시간);
                                    curCh = 100;
                                    }
                                    */
                            }
                        }
                        if(cmdp_channel != NULL) {
                            // url 정보가 있는 경우 재생할 url에 해당하는 stream ID
                            // url 정보가 없는 경우 뒤이어 전달되는 "media_data"(TTS)에 해당하는 stream ID
                            if(cmdp_channel->valueint != 100) {
                                curCh = cmdp_channel->valueint;
                            }
                        }
                        if(cmdp_url != NULL) {
                            sprintf(file_path, "aplay %s", cmdp_url->valuestring);
                            system(file_path);
                            // TODO: 실제 미디어 재생이 시작되면 재생 시작 이벤트를 SDK로 전달한다. 
                            agent_updateMediaStatus(curCh, "started", 0);
                            memset(file_path, 0x0, sizeof(file_path));
                                /*
                            std::string url(cmdp_url->valuestring);
                            if(player.play(0, url) == 0) {
                                agent_updateMediaStatus(curCh, "started", 0);
                                } 
                                */
                        }
                    }
                }
                cJSON_Delete(cmdp_payload);
                
                // play_media / media_data 가 수신되는 경우 미디어 재생이 시작되고 호출어 인식을 시작하는 것을 권고한다.
                // control_media / control_hardware / exec_dialogkit 등과 같이 별다른 미디어 재생없이 전달될 수 있는 메시지의 경우 해당 메시지 수신 후 바로 호출어 인식을 시작해도 무방하다. 
                if(g_start_kws == 0) {
                    g_start_kws = 1;
                }
            }
            else if(strcmp(actionType.c_str(), "control_media") == 0) 
            {
                // TODO: 특정 channel에 해당하는 미디어를 제어하기 위해 전달되는 메시지
                cJSON *cmdp_payload = cJSON_Parse(payload.c_str());
                if (cmdp_payload != NULL) {
                        // 제어해야 할 채널, 동작 등의 값이 전달된다.
                        // 예) channel=1, act="stop" 이라면, 1번 채널로 재생중인 미디어를 중지시키고 재생이 중지되면 agent_updateMediaStatus(1, "stopped", 재생시간)를 호출하여 SDK로 재생상태를 전달한다.
                    cJSON *cmdp_cmdOpt = cJSON_GetObjectItem(cmdp_payload, "cmdOpt");
                    if (cmdp_cmdOpt != NULL) {
                        // channel : 제어 대상 채널
                        cJSON *cmdp_channel = cJSON_GetObjectItem(cmdp_cmdOpt, "channel");
                        //act : 제어 동작 (pause/stop/resume)
                        cJSON *cmdp_act = cJSON_GetObjectItem(cmdp_cmdOpt, "act");

                        // TODO: channel에 해당하는 미디어를 act 동작에 따라 제어한다.
                  }
                  cJSON_Delete(cmdp_payload);                    
                }
                
                // TODO: 호출어 인식 중 상태가 아니면 호출어 인식 시작
                if(g_start_kws == 0) {
                    g_start_kws = 1;
                }
            }
            else if (strcmp(actionType.c_str(), "exec_dialogkit") == 0)
            {
                // TODO: dialogkit에 정의한 포맷대로 메시지를 파싱하고 처리한다.
                printf("exec_dialogkit, dialogResponse=\n%s\n", payload.c_str());
                if(g_start_kws == 0) {
                    g_start_kws = 1;
                }
            }
            else if (strcmp(actionType.c_str(), "control_hardware") == 0)
            {
                // volume이나 BT Classic(A2DP Sink) 제어를 위한 메시지
                // TODO: 볼륨 또는 블루투스 제어를 완료한 후 target, hwCmd, hwCmdResult, trxid 정보를 포함하여 응답 메시지를 SDK로 전달해야 한다. (SDK v1.1.1 버전부터 지원 예정)
                cJSON *cmdp_payload = cJSON_Parse(payload.c_str());
                if (cmdp_payload == NULL)
                {
                    printf("control_hardware parsing Error\n");
                }
                else
                {
                    cJSON *cmdp_cmdOpt = cJSON_GetObjectItem(cmdp_payload, "cmdOpt");
                    if (cmdp_cmdOpt != NULL)
                    {
                        // target : 제어 대상(volume or bluetooth)
                        cJSON *cmdp_target = cJSON_GetObjectItem(cmdp_cmdOpt, "target");
                        // hwCmd : 제어 타입
                        cJSON *cmdp_hwCmd = cJSON_GetObjectItem(cmdp_cmdOpt, "hwCmd");
                        // hwCmdOpt : 제어 명령/값 (예: 볼륨 제어의 경우 볼륨 업/다운, 업/다운 시 볼륨 값)
                        cJSON *cmdp_hwCmdOpt = cJSON_GetObjectItem(cmdp_cmdOpt, "hwCmdOpt");
                        // trxid : 제어명령 구분을 위한 transaction id
                        cJSON *cmdOpt_trxid = cJSON_GetObjectItem(cmdp_cmdOpt, "trxid");

                        // TODO: 볼륨 제어
                        if (strcmp(cmdp_target->valuestring, "volume") == 0)
                        {
                            if(strcmp(cmdp_hwCmd->valuestring, "setVolume") == 0 && cmdp_hwCmdOpt != NULL) {
                                cJSON *cmdp_control = cJSON_GetObjectItem(cmdp_hwCmdOpt, "control");
                                cJSON *cmdp_value = cJSON_GetObjectItem(cmdp_hwCmdOpt, "value");

                                printf("control_hardware: setVolme:(%s, %s)\n", cmdp_control->valuestring, cmdp_value->valuestring);
                                if (strcmp(cmdp_control->valuestring, "UP") == 0) {
                                    printf("volume up!\n");
                                } else {
                                    printf("volume down!\n");
                                    }
                                // TODO: 볼륨 제어 결과를 SDK로 전달한다. (SDK v1.1.1 버전부터 지원 예정)
                                sendResHwcl(cmdp_target->valuestring, cmdp_hwCmd->valuestring, "setVolume success", cmdOpt_trxid->valuestring);
                                }
                        }
                        // TODO: BT Classic(A2DP Sink) 제어
                        else if(strcmp(cmdp_target->valuestring, "bluetooth") == 0)
                        {
                            printf("control_hardware: target=%s, cmd=%s\n", cmdp_target->valuestring, cmdp_hwCmd->valuestring);
                        }
                    }
                }
                cJSON_Delete(cmdp_payload);
                
                if(g_start_kws == 0) {
                    g_start_kws = 1;
                }
            }
            else if (strcmp(actionType.c_str(), "webview_url") == 0)
            {
                  // 실행해야 할 web url 이 전달된다.
                cJSON *cmdp_payload = cJSON_Parse(payload.c_str());
                if (cmdp_payload == NULL)
                {
                    printf("webview_url parsing Error\n");
                }
                else
                {
                    cJSON *cmdp_cmdOpt = cJSON_GetObjectItem(cmdp_payload, "cmdOpt");
                    if (cmdp_cmdOpt != NULL)
                    {
                        cJSON *cmdp_oauth_url = cJSON_GetObjectItem(cmdp_cmdOpt, "oauth_url");
                        if(cmdp_oauth_url != NULL) {
                            // TODO: 해당 값이 온다면 지니뮤직 로그인이 필요한 상태이며 브라우저를 이용하여 지니뮤직에 로그인 하도록 유도한다.
                            printf("webview_url, oauth_url=%s\n", cmdp_oauth_url->valuestring);
                        }
                    }
                }
                cJSON_Delete(cmdp_payload);
                
                if(g_start_kws == 0) {
                    g_start_kws = 1;
                }
            }
            else
            {
                printf("OnCommandThread: wrong actionType=%s\n", actionType.c_str());
                if (actionType == "Res_TXCM")
                {
                    printf("Res_TXCM, msgPayload=%s\n", payload.c_str());
                }
                
                if(g_start_kws == 0) {
                    g_start_kws = 1;
                }
            }
            
            oncmd_mtx.lock();
            g_onCmdList.pop_front();
            oncmd_mtx.unlock();
        }
        
        SLEEP_MS(10);
    }

    THREAD_RETURN;
}

void onCommand(std::string actionType, std::string payload)
{
    printf("[onCommand] : actionType=%s\n", actionType.c_str());
    oncmd_mtx.lock();
    g_onCmdList.push_back({actionType, payload});
    oncmd_mtx.unlock();
}

void onEvent(int eventMask, std::string opt) {
    switch (eventMask) {
        case INSIDE_EVENT::SERVER_ERROR:
            /*
             * 음성인식이 특정 오류로 인해 실패했을 때 해당 이벤트가 전달된다.
             * 에러 종류를 확인하고 필요 시 사용자에게 알리고,
           * onCommand(stop_voice)와 동일하게 동작하되, 기존 일시정지 중인 미디어(뮤직, 라디오 등)가 있다면 필요 시 다시 재생하고, 호출어 인식모드로 전환한다.
             * 
             */
            printf("[onEvent] SERVER_ERROR (%s) received.\n", opt.c_str());
            if(g_start_mic) { // 음성인식 중단
                g_start_mic = 0;
            }
            if(g_start_kws == 0) { // 호출어 인식 시작
                g_start_kws = 1;
            }
            break;
        case INSIDE_EVENT::GO_TO_STANDBY:
            /*
             * 음성인식 결과가 오인식이거나 처리할 수 없는 발화패턴인 경우 전달된다.
             * 음성인식 시작이 가능한 상태(예: 호출어 인식 상태)로 전환한다.
             */
            if(g_start_mic) { // 음성인식 중단
                g_start_mic = 0;
            }
            if(g_start_kws == 0) { // 호출어 인식 시작
                g_start_kws = 1;
            }
            break;
        case INSIDE_EVENT::TIMEOUT_START_VOICE:
            /*
             * 음성인식 시작 요청(agent_startVoice) 후 "start_voice" command를 수신하지 못한 경우 전달된다.
             * 음성인식 시작이 가능한 상태(예: 호출어 인식 상태)로 전환한다.
             */
            if(g_start_kws == 0) { // 호출어 인식 시작
                g_start_kws = 1;
            }
            break;
        case INSIDE_EVENT::TIMEOUT_STOP_VOICE:
            /*
             * 음성인식 중 일정 시간 이후에도 "stop_voice" command를 수신하지 못한 경우 전달된다.
             * 음성인식을 중단하고 음성인식 시작이 가능한 상태(예: 호출어 인식 상태)로 전환한다.
             */
            if(g_start_mic) { // 음성인식 중단
                g_start_mic = 0;
            }
            if(g_start_kws == 0) { // 호출어 인식 시작
                g_start_kws = 1;
            }
            break;
        case INSIDE_EVENT::TIMEOUT_RECEIVE_DATA:
            /*
             * 음성인식 완료("stop_voice" command 수신)되고 일정 시간 이후에도 
             * 음성인식 결과에 따른 command("play_media", "control_media", "control_hardware" 등)가 수신되지 않는 경우 전달된다.
             * 사용자에게 상황을 알린 후,
             * 음성인식 시작이 가능한 상태(예: 호출어 인식 상태)로 전환한다.
             */
            if(g_start_kws == 0) { // 호출어 인식 시작
                g_start_kws = 1;
            }
            break;
        default:
            printf("[onEvent] eventMask=%d, opt=%s\n", eventMask, opt.c_str());
            if(g_start_kws == 0) {
                g_start_kws = 1;
            }
            break;
    }
}

int test_kws_init() {
//    // TODO : agent_init()을 통해 uuid 정보를 정상적으로 발급받은 상태에서 kws_init()이 호출되어야 한다.     
//    if(!isInit) {
//        printf("kws_init failed. call agent_init first!\n");
//        return -1;
//    }

    if(!isKwsInit) {
        // TODO : 호출어 모델 파일 경로가 "./conf/x.cnsf" 아닌 경우 경로를 지정해 주어야 한다.
        printf("> kws_setModelPath(): %d\n", kws_setModelPath("./conf"));
    
        int ret = kws_init();
        if(ret == 0) {
            printf("> kws_init()\n");
            isKwsInit = true;
            printf("> kws_setKeyword()\n");
            kws_setKeyword(1);
        } else {
            printf("kws_init() fail, ret=%d\n", ret);
            isKwsInit = false;
        }
        return ret;
    } else {
        printf("kws_init already completed.\n");
        return 0;
    }
}

int test_setServerInfo() {
    char    server[255], grpcPort[100], restPort[100];
    int     length, loop_exit = 0;
    FILE    *fp;

    // TODO : server_info.txt 파일 위치에 따라 경로를 변경한다.
    fp = fopen("./server_info.txt", "rt");
    if(fp != NULL)
    {
        fgets(server, sizeof(server), fp);
        length = strlen(server);
        if (server[length - 1] == '\n') server[length - 1] = 0x0;

        fgets(grpcPort, sizeof(grpcPort), fp);
        length = strlen(grpcPort);
        if (grpcPort[length - 1] == '\n') grpcPort[length - 1] = 0x0;

        fgets(restPort, sizeof(restPort), fp);
        length = strlen(restPort);
        if (restPort[length - 1] == '\n') restPort[length - 1] = 0x0;
        fclose(fp);

        printf("> agent_setServerInfo(): %s, grpcPort=%s, restPort=%s\n", server, grpcPort, restPort);
        agent_setServerInfo(server, grpcPort, restPort);
        return 0;
    }
    else
    {
        printf("set server failed : server_info.txt file not exist.\n");
        return -1;
    }
}

int main()
{
	FILE	*fp;
	char	szUuid[255];
	THREAD_HANDLE	hTimer, hMic, hOnCmd, hKws;
	REGISTER_CODE	rc;

	int		count, length;
	char	szTemp[255];
	// TODO : 발급받은 client_id, client_key, client_secret 정보를 별도 관리하여야 한다.
	fp = fopen("./key.txt", "rt");
	if (fp != NULL)
	{
		count = 0;
		while(!feof(fp))
		{
			memset(szTemp, 0x0, sizeof(szTemp));
			fgets(szTemp, sizeof(szTemp), fp);
			length = strlen(szTemp);
			if (szTemp[length - 1] == '\n') szTemp[length - 1] = 0x0;
			switch (count)
			{
			case 0:
			    g_clientId = szTemp;
				break;
			case 1:
			    g_clientKey = szTemp;
				break;
			case 2:
			    g_clientSecret = szTemp;
				break;
			}
			count++;
		}
		fclose(fp);
	}
	else
	{
		printf("Key file (key.txt) does not exist. so exit!\n");
		return 0;
	}
	printf("> config Client_ID=%s, Client_Key=%s, Client_Secret=%s\n", g_clientId.c_str(), g_clientKey.c_str(), g_clientSecret.c_str());
	
	// debug mode on : sample app 실행된 위치에 SDK 로그가 ginsdie.log 파일에 기록된다.
	// debuf mode off 시 agent_debugmode(false) 호출
	printf("> agent_debugmode(): on\n");
	agent_debugmode(true);
	
	// TODO : SDK가 연동할 Agent Server 정보를 설정한다. (server_info.txt 파일에 서버 정보 기록)
    if(test_setServerInfo() < 0) {
        return 0;
    }

    // TODO : SDK 연동 절차
    /*
     * 1) agent_register() : 발급받은 key(client_id, client_key, client_secret) 정보를 이용하여 uuid 발급
      *    - 이미 발급받은 uuid가 있는 경우 agent_register()를 중복 호출하지 않도록 한다.
     * 2) agent_init() : 발급받은 uuid 정보를 이용하여 Agent 서버와 gRPC 연결 및 onCommand/onEvent callback 등록
     * 3) kws_init() : 호출어 인식 시작
     * 
     */
    bool need_register = false;
    memset(szUuid, 0x0, sizeof(szUuid));
    // 이미 발급받은 uuid 정보가 있는 경우 agent_register()를 호출하지 않는다.
    fp = fopen("uuid.txt", "rt");
    if (fp != NULL)
    {
        fread(szUuid, 1, sizeof(szUuid), fp);
        fclose(fp);
    }

    g_uuid = szUuid;
    if (g_uuid.length() > 0)
    {
        printf("already regisgered.\n> agent_init()\n");    
        rc = agent_init(g_clientId, g_clientKey, g_clientSecret, g_uuid);
        if (rc.rc == 200)
        {
            printf("> agent_setCommandEventCallback()\n");
            agent_setCommandEventCallback(onCommand, onEvent);

            isInit = true;
        }
        else
        {
            printf("Error: agent_init(): rc=%d, rcmsg=%s\n", rc.rc, rc.rcmsg.c_str());
            if (rc.rc == 404)
            {
                need_register = true;
            }
            else 
            {
                return 0;
            }
        }
    } else {
        need_register = true;
    }
    
    rc.rc = 0;
    rc.uuid = "";
    rc.rcmsg = "";

    if(need_register) 
    {
        rc = agent_register(g_clientId, g_clientKey, g_clientSecret, "");
        if (rc.rc == 200)
        {
            // TODO : agent_register()를 통해 uuid를 발급 받으면 내부적으로 저장/관리하고, Service Agent 재실행 시 agent_register()를 중복 호출하지 않도록 한다. 
            printf("> agent_register(): uuid=%s\n", rc.uuid.c_str());
            g_uuid = rc.uuid;
            fp = fopen("uuid.txt", "wt");
            if (fp != NULL)
            {
                fwrite(rc.uuid.c_str(), 1, rc.uuid.length(), fp);
                fclose(fp);
            }
            printf("> agent_init()\n");    
            rc = agent_init(g_clientId, g_clientKey, g_clientSecret, g_uuid);
            if (rc.rc == 200)
            {
                printf("> agent_setCommandEventCallback()\n");
                agent_setCommandEventCallback(onCommand, onEvent);

                isInit = true;
            }
            else
            {
                if (rc.rc == 404)
                {
                    // TODO : agent_register()가 성공할 때까지 재시도하거나 사용자에게 알리고 종료한다.
                    printf("Error: agent_init() got %d, %s\n",rc.rc, rc.rcmsg.c_str());
                    return 0;
                }
            }

        }
        else
        {
            printf("Error: agent_register got %d, %s\n", rc.rc, rc.rcmsg.c_str());
            return 0;
        }
    }
    
    test_kws_init();
    
    // TODO : 알람/타이머, 음성인식, 호출어 인식 등을 위한 Thread를 생성한다.
    hTimer = SpawnThread(TimerThread, NULL);
    hMic = SpawnThread(MicThread, NULL);
    hOnCmd = SpawnThread(OnCommandThread, NULL);  
    hKws = SpawnThread(KwsThread, NULL);  

    std::string gettts_result;
    int ttsDataSize = 0;
    const char* ttsData;
    cJSON *cmdp_jsonObj;
    
    int ret = 0;

	while (true)
	{	   
		usleep(1000);
	}


#ifdef  LINUX
	pthread_cancel(hTimer);
	pthread_cancel(hMic);
	pthread_cancel(hOnCmd);
	pthread_cancel(hKws);
#endif

	printf("Exit!\n");
}
