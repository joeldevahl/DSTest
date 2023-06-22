#include "Render.h"
#include "Generator.h"

#include "shellapi.h"
#include "stdlib.h"

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    Render* render = reinterpret_cast<Render*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_CREATE:
		{
			LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
			SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
		}
		return 0;

    case WM_PAINT:
        if (render)
        {
            Draw(render);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    const UINT width = 1280;
    const UINT height = 720;

    char* generatorFileName = nullptr;
    char* generatorFileNameBin = nullptr;

    int numArgs = 0;
    LPWSTR* args = CommandLineToArgvW(lpCmdLine, &numArgs);
    if (args)
    {
        int ia = 0;
        while (ia < numArgs)
        {
            if (wcscmp(args[ia], L"-generate") == 0)
            {
                ia += 1;

                if (ia >= numArgs)
                    return -1;

                int numBytes = wcstombs(nullptr, args[ia], 0) + 1;
                generatorFileName = new char[numBytes];

                numBytes = wcstombs(generatorFileName, args[ia], numBytes);
            }
            else if (wcscmp(args[ia], L"-bin") == 0)
            {
                ia += 1;

                if (ia >= numArgs)
                    return -1;

                int numBytes = wcstombs(nullptr, args[ia], 0) + 1;
                generatorFileNameBin = new char[numBytes];

                wcstombs(generatorFileNameBin, args[ia], numBytes);
            }

            ia += 1;
        }
        LocalFree(args);
    }

    WNDCLASSEX windowClass = { 0 };
    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = hInstance;
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowClass.lpszClassName = L"DXSampleClass";
    RegisterClassEx(&windowClass);

    RECT windowRect = { 0, 0, width, height };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    if (generatorFileName)
        Generate(generatorFileName, generatorFileNameBin);
    
    Render* render = CreateRender(width, height);

    HWND hwnd = CreateWindow(
        windowClass.lpszClassName,
        L"MeshShader",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        hInstance,
        render);

    Initialize(render, hwnd);
   
    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    Destroy(render);

    if (generatorFileName)
        delete[] generatorFileName;
    if (generatorFileNameBin)
        delete[] generatorFileNameBin;

    return static_cast<char>(msg.wParam);
}
