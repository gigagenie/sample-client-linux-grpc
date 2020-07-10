# sample-client-linux-grpc

Linux Sample Client (using g-sdk-linux lib)

Linux 기반으로 동작하는 GiGA Genie Inside(이하, G-INSIDE) 샘플 클라이언트입니다.
G-INSIDE Linux Device SDK(gRPC 버전)를 이용하여 구현된 것으로서 이 오픈소스를 통해 Device SDK의 이용 방법을 참고하실 수 있습니다.

## GiGA Genie Inside
GiGA Genie Inside(이하, G-INSIDE)는 3rd party 개발자가 자신들의 제품(단말 장치, 서비스, 앱 등)에 KT의 AI Platform인 
'기가지니'를 올려서 음성인식과 자연어로 제어하고 기가지니가 제공하는 서비스(생활비서, 뮤직, 라디오 등)를 사용할 수 있도록 해줍니다.

G-INSIDE는 기가지니가 탑재된 제품을 개발자들이 쉽게 만들 수 있도록 개발 도구와 문서, 샘플 소스 등 개발에 필요한 리소스를 제공합니다.

## Prerequisites

### 인사이드 클라이언트 키 발급
  1. [API Link](https://apilink.kt.co.kr) 에서 회원가입 
  2. 사업 제휴 신청 및 디바이스 등록 (Console > GiGA Genie > 인사이드 디바이스 등록)
  3. 디바이스 등록 완료 후 My Device에서 등록한 디바이스 정보 및 클라이언트 개발키 발급 확인 (Console > GiGA Genie > My Device)

### 개발 환경
*   OS: Ubuntu Linux
*   Build Tool: g++
*   [G-SDK for Linux](https://github.com/gigagenie/ginside-sdk/blob/master/g-sdk-linux) : SDK 사용을 위해 [README](https://github.com/gigagenie/ginside-sdk/blob/master/g-sdk-linux/README.md) 내용을 참고한다. 

### 필수 라이브러리 및 파일
*   [SDK 라이브러리](https://github.com/gigagenie/ginside-sdk/tree/master/g-sdk-linux/lib) : ubuntu-x86_64용 libginside.so, libKwsRnet.so 파일을 다운로드 한다.
*   [SDK 헤더파일](https://github.com/gigagenie/ginside-sdk/tree/master/g-sdk-linux/include) : ginside.h, ginsidedef.h 파일을 다운로드 한다.
*   [호출어 모델 파일](https://github.com/gigagenie/ginside-sdk/tree/master/g-sdk-linux/conf) : 호출어 인식을 위한 모델파일을 다운로드 한다.
*   ALSA library : 호출어/음성인식을 위한 voice recording 및 미디어 재생을 위해 ALSA 라이브러리를 설치한다.
    ```
    $ sudo apt-get install libasound2-dev libasound2
    ```

## 소스 디렉토리(src/) 내 파일 구성
*   conf/ : 호출어 모델 파일 저장 위치
*   include/ : SDK용 헤더파일 저장 위치 (ginside.h, ginsidedef.h)
*   lib/ : SDK 라이브러리 저장 위치 (libginside.so, libKwsRnet.so)
*   key.txt : API Link에서 발급받은 개발키 정보를 저장
*   server_info.txt : G-INISDE 개발 서버 정보 저장
*   test_sample.cpp : sample app main 소스

## Linux용 Sample 빌드
- sample-client-linux-grpc를 다운로드 후 src/ 로 이동
- Makefile에서 lpthread, lcjson, lasound 라이브러리와 헤더파일의 위치를 수정한다.
- SDK 라이브러리, 헤더파일, 호출어 모델 파일 등을 lib/, include/, conf/ 디렉토리에 각각 복사하고 Makefile에서 경로 정보를 수정한다.
- 실제 발급받은 클라이언트 키값을 key.txt 파일에 입력한다.
    ```
    YOUR-CLIENT-ID
    YOUR-CLIENT-KEY
    YOUR-CLIENT-SECRET
    ```
- 빌드가 완료되면 실행 파일(test_sample)이 생성된다.
    ```
    $ make
    ```

## Sample App 실행
- test_sample 실행 전 SDK 라이브러리의 path를 설정하기 위해 다음 명령을 입력하고 실행한다.
  (별도로 /lib 위치를 변경하지 않은 경우 ./lib가 SDK 라이브러리의 위치가 된다.)
    ```
    $ export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH;
    ```
- test_sample 실행하면 다음과 같은 절차로 동작한다.
  1. 개발키 정보를 key.txt파일로부터 읽어 client id, client key, client secret 정보를 설정한다.
  2. 디폴트로 디버그 모드를 설정한다.
  3. 연동할 서버정보를 server_info.txt 파일로부터 읽어 설정한다.
  4. 이미 발급받은 uuid 정보가 있는지 체크한다. 
     - uuid 정보가 있는 경우 해당 uuid를 이용하여 agent_init을 호출하여 서버와 gRPC 연결을 시도하고,
     - uuid 정보가 없는 경우 agent_register를 호출하여 uuid 발급을 요청한다. 
  5. agent_init 결과가 rc=404(uuid not found) 에러인 경우 agent_register를 통해 uuid를 재발급 받는다.
  6. 서버와 gRPC 연결이 완료되면 onCommand, onEvent callback을 등록하고, 
  7. 호출어 인식을 위해 호출어 모델파일 경로를 설정하여 kws SDK를 초기화(kws_init)하고, 사용할 호출어(기가지니, 지니야, 친구야, 자기야)를 설정한다.
     - 디폴트 호출어는 '지니야'로 설정되어 있다.
  8. 호출어, 음성인식, 알람/타이머 등 처리를 위해 Thread를 각각 동작 시키고, 호출어 인식을 시작한다.


## License

sample-client-linux-grpc is licensed under the [Apache License 2.0](http://www.apache.org/licenses/LICENSE-2.0)

