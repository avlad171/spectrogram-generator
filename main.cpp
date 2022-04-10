//display
#include <windows.h>
#include <gl/gl.h>

//audio
#include "bass.h"

//stl
#include <iostream>
#include <fstream>
#include <sstream>
#include <math.h>
#include <string.h>
#include <string>

//ffmpeg
#include "ffmpeg_encoder.h"

using namespace std;

//window procedure
LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
void EnableOpenGL(HWND hwnd, HDC*, HGLRC*);
void DisableOpenGL(HWND, HDC, HGLRC);

//helper to display error
void DisplayError(string errormsg)
{
    stringstream err;
    err<<errormsg<<" (code "<<BASS_ErrorGetCode()<<").";
    MessageBoxA(NULL, err.str().c_str(), "ERROR", MB_OK | MB_ICONERROR);
}

//function to play audio file, returns the bass channel
int PlayFile(const char * path)
{
    cout<<"PlayFile called!\n";
    int channel = 0;
    if (!(channel = BASS_StreamCreateFile(FALSE, path, 0, 0, BASS_SAMPLE_LOOP | BASS_SAMPLE_FLOAT)) &&
        !(channel = BASS_MusicLoad(FALSE, path, 0, 0, BASS_MUSIC_RAMPS | BASS_SAMPLE_LOOP | BASS_SAMPLE_FLOAT, 1)))
    {
		DisplayError("Can't play file");
		return 0;
	}

	BASS_ChannelPlay(channel, FALSE);

	return channel;
}

int LoadAudioFile(const char * path)
{
    string errmsg = "Can't load file: ";
    HSTREAM channel = 0;
    if (!(channel = BASS_StreamCreateFile(FALSE, path, 0, 0, BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_STREAM_PRESCAN)))
    {
		DisplayError(errmsg + path);
		return 0;
	}

	return channel;
}

float * GetAudioData(int channel, int window_size)
{
        BASS_CHANNELINFO ci;
        BASS_ChannelGetInfo(channel, &ci);  //get number of channels, 1=mono,2=stereo etc
        float * buf = new float [ci.chans * window_size];
        cout<<ci.chans * window_size<<endl;
        BASS_ChannelGetData(channel, buf, ci.chans * window_size * sizeof(float));

        return buf;
}

float k = 0;
float re = 0;
float im = 0;
HDC hGlobalDC;

unsigned int getCurrentFrameData(uint8_t * rgb, int width, int height)
{
    uint8_t * buf = new uint8_t [width * height * 3];

    glReadBuffer(GL_FRONT);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, buf);

    //now flip image
    for (int i = 0; i < height; ++i)
        for(int j = 0; j < 3 * width; ++j)
        {
            //rgb[height - i][j] = buf[i][j];
            rgb[(height - i) * width * 3 + j] = buf[i * width * 3 + j];
        }
    delete [] buf;
    return glGetError();
}

bool SaveAsPPM(const char *filename)
{
    int width = 720;
    int height = 576;
    cout<<"[BMP] Width = "<<width<<", height = "<<height<<"\n";

    unsigned char * imgdata = new unsigned char [width * height * 3];
    glReadBuffer(GL_FRONT);
    glReadPixels(0, 0, width, height,GL_RGB, GL_UNSIGNED_BYTE, imgdata);
    cout<<"Last error: "<<glGetError()<<"\n";

    ofstream img (filename);
    img<<"P3\n"<<width<<" "<<height<<"\n256\n";
    for(int i = 0; i < height; ++i)
    {
        for(int j = 0; j < width * 3; ++j)
        {
            img<<(unsigned int)imgdata[i * width * 3 + j]<<" ";//<<(unsigned int)imgdata[i * width + j + 1]<<" "<<(unsigned int)imgdata[i * width + j + 2]<<" ";
        }
        img<<"\n";
    }
    img.close();
    return true;
}

int WINAPI WinMain(HINSTANCE hInstance,
                   HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine,
                   int nCmdShow)
{
    WNDCLASSEX wcex;
    HWND hwnd;
    HDC hDC;
    HGLRC hRC;
    MSG msg;
    BOOL bQuit = FALSE;
    float theta = 0.0f;

    /* register window class */
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_OWNDC;
    wcex.lpfnWndProc = WindowProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = "Spectrum";
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);;

    if (!RegisterClassEx(&wcex))
        return 0;

    int mode = 0;
    //cout<<"Choose mode:\n0 - display waveform\n1 - Fourier transform\nYour choice: ";
    //cin>>mode;
    mode = 3;

    if(mode == 0)
        k = 24000;

    else
        k = 10;

    /* create main window */
    hwnd = CreateWindowEx(0,
                          "Spectrum",
                          "Spectrum Generator",
                          WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT,
                          CW_USEDEFAULT,
                          1280,
                          720,
                          NULL,
                          NULL,
                          hInstance,
                          NULL);

    ShowWindow(hwnd, nCmdShow);

    // initialize BASS
    if (!BASS_Init(-1, 44100, 0, hwnd, NULL))
    {
        DisplayError("BASS_Init failed!");
        return -1;
    }

    cout<<"Command line: "<<lpCmdLine<<endl;

    string filename = lpCmdLine;
    if(lpCmdLine[0] == '"')
        filename = filename.substr(1, filename.size() - 2);

    cout<<"Opening "<<filename<<"\n";


    int channel = 0;
    if(mode < 2)
        channel = PlayFile(filename.c_str());
    else
        channel = LoadAudioFile(filename.c_str());

    //split extension
    filename = filename.substr(0, filename.find_last_of("."));
    string outname = filename + ".mp4";

    //get input information
    BASS_CHANNELINFO ci;
    BASS_ChannelGetInfo(channel, &ci);  //get number of channels, 1=mono,2=stereo etc

    float audio_bitrate;
    float audio_samplerate;

    if(!BASS_ChannelGetAttribute(channel, BASS_ATTRIB_BITRATE, &audio_bitrate))
    {
        DisplayError("BASS can't get input bitrate!");
        return -1;
    }

    if(!BASS_ChannelGetAttribute(channel, BASS_ATTRIB_FREQ, &audio_samplerate))
    {
        DisplayError("BASS can't get input samplerate!");
        return -1;
    }

    cout<<"Audio file contains "<<ci.chans<<" channels!\n";
    cout<<"Audio stream is encoded with "<<audio_bitrate<<" kbps and has a samplerate of "<<audio_samplerate<<" Hz\n";

    // length in bytes
    int audiolen_bytes = BASS_ChannelGetLength(channel, BASS_POS_BYTE);
    int last_sample_retrieved = 0;
    // the time length
    int audiolen_seconds = (int)BASS_ChannelBytes2Seconds(channel, audiolen_bytes);
    int audiolen_minutes = audiolen_seconds / 60;
    audiolen_seconds = audiolen_seconds % 60;

    int window_size = 65536;
    //get audio data
    float * buf = new float [ci.chans * window_size];

    //initialize ffmpeg encoder
    uint8_t * imdata = new uint8_t [1280 * 720 * 3];
    ffmpeg_encoder enc(outname.c_str(), 1280, 720, 500000, 30, /*(int)audio_bitrate * 1000*/64000, (int)audio_samplerate);
    if(!enc.init(AV_CODEC_ID_H264, AV_CODEC_ID_MP3))
    {
        return 0;
    }

    /* enable OpenGL for the window */
    EnableOpenGL(hwnd, &hDC, &hRC);
    hGlobalDC = hDC;

    HFONT	font;										// Windows Font ID

	glGenLists(96);								// Storage For 96 Characters

	font = CreateFont(	-24,							// Height Of Font
						0,								// Width Of Font
						0,								// Angle Of Escapement
						0,								// Orientation Angle
						FW_BOLD,						// Font Weight
						FALSE,							// Italic
						FALSE,							// Underline
						FALSE,							// Strikeout
						ANSI_CHARSET,					// Character Set Identifier
						OUT_TT_PRECIS,					// Output Precision
						CLIP_DEFAULT_PRECIS,			// Clipping Precision
						ANTIALIASED_QUALITY,			// Output Quality
						FF_DONTCARE|DEFAULT_PITCH,		// Family And Pitch
						"Helvetica Rounded");					// Font Name

	SelectObject(hDC, font);							// Selects The Font We Want

	//wglUseFontBitmaps(hDC, 32, 96, base);				// Builds 96 Characters Starting At Character 32

    /* program main loop */
    bool done = false;
    int frame = 0;
    double fps = 30;
    double period = 1./fps;

    while (!bQuit)
    {
        /* check for messages */
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            /* handle or dispatch messages */
            if (msg.message == WM_QUIT)
            {
                bQuit = TRUE;
            }
            else
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        else
        {
            /* OpenGL animation code goes here */
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            glPushMatrix();

            if (mode == 0)
            {
                BASS_ChannelGetData(channel, buf, ci.chans * window_size * sizeof(float));

                //display waveform
                for (int c = 0; c < ci.chans; ++c)
                {
                    glLineWidth(2.0f);
                    glBegin(GL_LINE_STRIP);

                    if(c == 0)
                        glColor3f(1.0f, 0.0f, 0.0f);    //ch1 red
                    else if (c == 1)
                        glColor3f(0.0f, 1.0f, 0.0f);    //ch2 green
                    else
                        glColor3f(0.5f, 1.0f, 1.0f);

                    if (k > window_size - 1)
                        k = window_size - 1;

                    if (k < 1)
                        k = 1;

                    for (int i = c; i < ci.chans * (int)(k); i = i + ci.chans)
                    {
                        //dot 0 is at x = -1.0
                        //dot [k] is at x = 1.0
                        float x = (i - c) / ci.chans + c;
                        x = x / (k / 2.);
                        x -= 1;

                        glVertex2f(x, -1.0 +
                                        (2. * (float)c)/(float)ci.chans +
                                        1./(1. * ci.chans) +
                                        (buf[i] / (float)(ci.chans * 1)));
                    }

                    glEnd();
                }
                //write with white
                glColor3f(1.0f, 1.0f, 1.0f);
                // create bitmaps for the device context font's first 256 glyphs
                wglUseFontBitmaps(hDC, 0, 256, 1000);

                // move bottom left, southwest of the red triangle
                glRasterPos2f(-1.0f, 0.9);

                // set up for a string-drawing display list call
                glListBase(1000);

                //create text
                char text [128];
                sprintf(text, "Waveform view. %d channels, sliding window is %d samples.", ci.chans, (int)k);

                // draw a string using font display lists
                glCallLists(strlen(text), GL_UNSIGNED_BYTE, text);

                glRasterPos2f(-1.0f, 0.95);
                sprintf(text, "Now playing: %s", filename.c_str());
                glCallLists(strlen(text), GL_UNSIGNED_BYTE, text);
            }

            else if (mode == 1)
            {
                BASS_ChannelGetData(channel, buf, ci.chans * window_size * sizeof(float));
                glShadeModel(GL_SMOOTH);

                glLineWidth(1.0f);
                glBegin(GL_LINES);
                    glColor3f(1.0f, 1.0f, 1.0f);
                    glVertex2f(-1.0, 0);
                    glVertex2f(1.0, 0);
                glEnd();

                glBegin(GL_LINES);
                    glColor3f(1.0f, 1.0f, 1.0f);
                    glVertex2f(0, -1.0);
                    glVertex2f(0, 1.0);
                glEnd();

                for (int c = 0; c < ci.chans; ++c)
                {
                    glLineWidth(2.0f);
                    glBegin(GL_LINE_STRIP);

                        if(c == 0)
                            glColor3f(1.0f, 0.0f, 0.0f);    //ch1 red
                        else if (c == 1)
                            glColor3f(0.0f, 1.0f, 0.0f);    //ch2 green
                        else
                            glColor3f(0.5f, 1.0f, 1.0f);

                        re = 0;
                        im = 0;
                        for (int i = c; i < ci.chans * window_size; i = i + ci.chans)
                        {
                            float x = (i - c) / ci.chans + c;
                            x = x / ((float)window_size / 2.);

                            float y = buf[i] / (float)(ci.chans * 4) + 0.25 + (float)c / (3 * ci.chans);

                            theta = 2 * 3.1415 * k;

                            float xf = y * cos(-2. * 3.1415 / (float)(window_size) * i * k);
                            float yf = y * sin(-2. * 3.1415 / (float)(window_size) * i * k);

                            re += xf;
                            im += yf;

                            glVertex2f(xf, yf);
                        }

                    glEnd();
                }
                //write with white
                glColor3f(1.0f, 1.0f, 1.0f);

                // create bitmaps for the device context font's first 256 glyphs
                wglUseFontBitmaps(hDC, 0, 256, 1000);

                // move bottom left, southwest of the red triangle
                glRasterPos2f(-1.0f, 0.9);

                // set up for a string-drawing display list call
                glListBase(1000);

                //create text
                char text [128];
                sprintf(text, "Fourier transform (%02f rad/s, %02f Hz) result: %06f (%06f + %06fi)\n", k * 4.24, k / 1.48, sqrt(re * re + im * im), re, -im);

                // draw a string using font display lists
                glCallLists(strlen(text), GL_UNSIGNED_BYTE, text);

                // move bottom left, southwest of the red triangle
                glRasterPos2f(-1.0f, 0.95);
                sprintf(text, "Now playing: %s", filename.c_str());
                glCallLists(strlen(text), GL_UNSIGNED_BYTE, text);
            }

            else if (mode == 2)
            {
                float fftdata[1024];
                BASS_ChannelGetData(channel, fftdata, BASS_DATA_FFT2048); // get the FFT data, 1024 bins

                for (int i = 0; i < 128; ++i)
                {
                    float x = i;
                    x = x / 64.0f;
                    x = x - 1.0f;

                    float y = 0;
                    for (int j = i * 8; j < i * 8 + 8; ++j)
                        y += fftdata[j];
                    //y /= 8.0f;

                    if (y > 1)
                        y = 1;

                    glBegin(GL_POLYGON);
                    glColor3f(0.0f, 1.0f, 0.0f);
                    glVertex2f(x, 0.01f);
                    glVertex2f(x - 1/64.0f, 0.01f);

                    glColor3f(y, 1.0f - y, 0.0f);
                    glVertex2f(x - 1/64.0f, y + 0.01f);
                    glVertex2f(x, y + 0.01f);
                    glEnd();
                }

                for (int i = 0; i < 1024; ++i)
                {
                    float x = i;
                    x = x / 512.0f;
                    x = x - 1.0f;

                    float y = fftdata[i] * 4;
                    if (y > 1)
                        y = 1;

                    glBegin(GL_LINES);
                    glLineWidth(2.0f);
                    glColor3f(0.0f, 1.0f, 0.0f);
                    glVertex2f(x, -0.01f);
                    glColor3f(y, 1.0f -y, 0.0f);
                    glVertex2f(x, -y - 0.01f);
                    glEnd();
                }
            }

            else if (mode == 3)
            {
                //logarithmic fft
                float fftdata[1024];
                int64_t startfrom = BASS_ChannelSeconds2Bytes(channel, period * frame);
                last_sample_retrieved = startfrom + 1024;
                //cout<<"Frame "<<frame<<", starting from t = "<<period * frame<<", b = "<<startfrom<<"\n";
                //set the correct time in the channel
                if(!BASS_ChannelSetPosition(channel, startfrom, BASS_POS_BYTE))
                {
                    //DisplayError("Setting position: ");
                    //audio reached end, exit
                    PostQuitMessage(0);
                }

                // get the FFT data, 1024 bins
                if(!BASS_ChannelGetData(channel, fftdata, BASS_DATA_FFT2048 | BASS_DATA_NOREMOVE))
                {
                    DisplayError("extracting channel data (FFT)!");
                }

                //for(int i = 0; i < 16; ++i)
                //    cout<<fftdata[i]<<" ";
                //cout<<endl;

                int b0 = 0;
                int bands = 128;
                float offset = 0.6;

                for (int i = 0; i < bands; i++)
                {
                    float x = i;
                    x = x / (float)(bands / 1.8);
                    x -= 0.9;

                    float peak = 0;
                    int b1 = pow(2, x * 10.0 / (bands - 1));
                    if (b1 <= b0)
                        b1 = b0 + 1; // make sure it uses at least 1 FFT bin
                    if (b1 > 1023)
                        b1 = 1023;
                    for (; b0 < b1; b0++)
                        if (peak < fftdata[b0])
                            peak = fftdata[b0];
                    float y = sqrt(peak) * 2; // scale it (sqrt to make low values more visible)
                    if (y > 1)
                        y = 1; // cap it

                    //cout<<peak<<" "<<x<<" "<<y<<"\n";

                    glBegin(GL_POLYGON);
                    glColor3f(0.0f, 1.0f, 0.0f);
                    glVertex2f(x, -0.99f + offset);
                    glVertex2f(x - 1/(bands / 1.), -0.99f + offset);

                    glColor3f(y, 1.0f, y);
                    glVertex2f(x - 1/(bands / 1.), y -0.99f + offset);
                    glVertex2f(x, y -0.99f + offset);
                    glEnd();
                }

                //now add text
                //write with white
                glColor3f(0.0f, 1.0f, 0.0f);

                // create bitmaps for the device context font's first 256 glyphs
                wglUseFontBitmaps(hDC, 0, 256, 1000);

                // move bottom left, southwest of the red triangle
                glRasterPos2f(-0.91f, -0.48);

                // set up for a string-drawing display list call
                glListBase(1000);

                //create text
                char text [128];
                sprintf(text, "Now playing: %s", filename.c_str());
                glCallLists(strlen(text), GL_UNSIGNED_BYTE, text);

                //create timer
                char timer [12];
                int current_postion_bytes = BASS_ChannelGetPosition(channel, BASS_POS_BYTE);
                int current_postion_seconds = (int)BASS_ChannelBytes2Seconds(channel, current_postion_bytes);
                int current_postion_minutes = current_postion_seconds / 60;
                current_postion_seconds = current_postion_seconds % 60;

                sprintf(timer, "%02d:%02d/%02d:%02d", current_postion_minutes, current_postion_seconds, audiolen_minutes, audiolen_seconds);
                glRasterPos2f(0.683f, -0.48);
                glCallLists(strlen(timer), GL_UNSIGNED_BYTE, timer);
            }
            else
                cout<<"Unsupported mode!\n";
                /*
                else if (specmode == 1)
                { // logarithmic, combine bins
                    int b0 = 0;
                    memset(specbuf, 0, SPECWIDTH * SPECHEIGHT);
#define BANDS 28
                    for (x = 0; x < BANDS; x++)
                    {
                        float peak = 0;
                        int b1 = pow(2, x * 10.0 / (BANDS - 1));
                        if (b1 <= b0)
                            b1 = b0 + 1; // make sure it uses at least 1 FFT bin
                        if (b1 > 1023)
                            b1 = 1023;
                        for (; b0 < b1; b0++)
                            if (peak < fft[1 + b0])
                                peak = fft[1 + b0];
                        y = sqrt(peak) * 3 * SPECHEIGHT - 4; // scale it (sqrt to make low values more visible)
                        if (y > SPECHEIGHT)
                            y = SPECHEIGHT; // cap it
                        while (--y >= 0)
                            memset(specbuf + y * SPECWIDTH + x * (SPECWIDTH / BANDS), y + 1, SPECWIDTH / BANDS - 2); // draw bar
                    }
                }
                */

            glPopMatrix();

            SwapBuffers(hDC);

            //encode ffmpeg
            getCurrentFrameData(imdata, 1280, 720);
            enc.encode_video_frame(imdata);
            //increment frame counter
            frame++;
        }
    }

    /* shutdown OpenGL */
    DisableOpenGL(hwnd, hDC, hRC);

    /* destroy the window explicitly */
    DestroyWindow(hwnd);

    //add audio to ffmpeg video
    {
        if(last_sample_retrieved > audiolen_bytes)
            last_sample_retrieved = audiolen_bytes;

        float * buf = new float[enc.get_audio_frame_size() * ci.chans];
        float * buf_mono = new float[enc.get_audio_frame_size()];
        int sample = 0;

        memset(buf, 0, enc.get_audio_frame_size() * ci.chans * sizeof(float));
        memset(buf_mono, 0, enc.get_audio_frame_size() * sizeof(float));
        cout<<enc.get_audio_frame_size()<<"\n";
        while(sample < last_sample_retrieved)
        {
            cout<<"encoding audio sample at "<<sample<<"/"<<audiolen_bytes<<"\n";
            int err = BASS_ChannelSetPosition(channel, sample, BASS_POS_BYTE);
            if(err == BASS_ERROR_POSITION)
            {
                //reached end
                break;
            }

            if(!BASS_ChannelGetData(channel, buf, enc.get_audio_frame_size() * ci.chans * sizeof(float) | BASS_DATA_NOREMOVE))
            {
                cout<<"Failed to extract data from audio stream to feed to ffmpeg!\n";
            }

            //convert to mono
            for(int i = 0; i < enc.get_audio_frame_size(); ++i)
            {
                buf_mono[i] = 0;
                for (int j = 0; j < ci.chans; ++j)
                    buf_mono[i] += buf[i * ci.chans + j];
                buf_mono[i] = buf_mono[i] / (float)ci.chans;
                //cout<<buf_mono[i]<<" ";

            }
            //cout<<"\n\n";

            enc.encode_audio_frame(buf_mono);
            sample += enc.get_audio_frame_size() * ci.chans * sizeof(float);
        }
        delete [] buf;
        delete [] buf_mono;
    }

    //finalize ffmpeg
    enc.finish();

    return msg.wParam;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static float scale = 100;
    switch (uMsg)
    {
        case WM_CLOSE:
            PostQuitMessage(0);
        break;

        case WM_DESTROY:
            return 0;

        case WM_KEYDOWN:
        {
            switch (wParam)
            {
                case VK_ESCAPE:
                    PostQuitMessage(0);
                    break;
                case VK_LEFT:
                    k -= 0.01 * scale;
                    cout<<"[DEBUG] K = "<<k<<endl;
                    break;
                case VK_RIGHT:
                    k += 0.01 * scale;
                    cout<<"[DEBUG] K = "<<k<<endl;
                    break;
                case VK_UP:
                    scale *= 10.0;
                    cout<<"[DEBUG] Scale = "<<scale<<endl;
                    break;
                 case VK_DOWN:
                    scale /= 10.0;
                    cout<<"[DEBUG] Scale = "<<scale<<endl;
                    break;
                 case VK_TAB:
                     cout<<"TAB!\n";
                    //save bitmap
                    //SaveAsBMP2("test.bmp");
                    SaveAsPPM("test.ppm");
                    break;
            }
        }
        break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    return 0;
}

void EnableOpenGL(HWND hwnd, HDC* hDC, HGLRC* hRC)
{
    PIXELFORMATDESCRIPTOR pfd;

    int iFormat;

    /* get the device context (DC) */
    *hDC = GetDC(hwnd);

    /* set the pixel format for the DC */
    ZeroMemory(&pfd, sizeof(pfd));

    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW |
                  PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 16;
    pfd.iLayerType = PFD_MAIN_PLANE;

    iFormat = ChoosePixelFormat(*hDC, &pfd);

    SetPixelFormat(*hDC, iFormat, &pfd);

    /* create and enable the render context (RC) */
    *hRC = wglCreateContext(*hDC);

    wglMakeCurrent(*hDC, *hRC);
}

void DisableOpenGL (HWND hwnd, HDC hDC, HGLRC hRC)
{
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(hRC);
    ReleaseDC(hwnd, hDC);
}

