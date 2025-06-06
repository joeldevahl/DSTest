#include "Render.h"
#include "Generator.h"

#include "shellapi.h"
#include "stdlib.h"

#include "imgui.h"

#include "commdlg.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    Render* render = reinterpret_cast<Render*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
        return true;

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
    int generatorLod = 0;
    bool useWarp = false;
    bool useWorkGraph = false;

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
            else if (wcscmp(args[ia], L"-lod") == 0)
            {
                ia += 1;

                if (ia >= numArgs)
                    return -1;

                generatorLod = _wtoi(args[ia]);
            }
            else if (wcscmp(args[ia], L"-warp") == 0)
            {
                useWarp = true;
            }
            else if (wcscmp(args[ia], L"-workGraph") == 0)
            {
                useWorkGraph = true;
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
        Generate(generatorFileName, generatorLod);
    
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


    wchar_t szFile[260];
    HANDLE hf;

    OPENFILENAME openFileName = { 0 };
    openFileName.lStructSize = sizeof(openFileName);
    openFileName.hwndOwner = hwnd;
    openFileName.hInstance = hInstance;
    openFileName.lpstrFile = szFile;
    openFileName.lpstrFile[0] = '\0';
    openFileName.nMaxFile = sizeof(szFile);
    openFileName.lpstrFilter = L"glb\0*.glb\0";
    openFileName.nFilterIndex = 1;
    openFileName.lpstrFileTitle = NULL;
    openFileName.nMaxFileTitle = 0;
    openFileName.lpstrInitialDir = NULL;
    openFileName.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    //BOOL couldOpen = GetOpenFileName(&openFileName);

    Initialize(render, hwnd, useWarp);
    SetWorkGraph(render, useWorkGraph);
   
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

    return static_cast<char>(msg.wParam);
}
