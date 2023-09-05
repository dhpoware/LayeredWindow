/* Copyright (c) 2005-2006, 2021, 2023 dhpoware. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/* This demo shows you how to create a window with a non-rectangular shape.
 * Older versions of Windows allowed you to do this by using regions. However
 * Windows 2000 and XP provides a more elegant method to do this using the so
 * called layered windows API.
 *
 * This demo creates a per pixel alpha blended layered window. The actual shape
 * of the window is stored in a 32-bit TGA file. When creating the TGA file
 * keep in mind that you *must* include an alpha mask in the TGA file if you
 * want the resulting window to have a non-rectangular shape. The layered
 * window API uses the alpha mask (stored in the TGA file) to create the
 * non-rectangular window.
 *
 * The hardest part of using the layered window API is creating a TGA file with
 * an alpha mask in it. Try not to feather your alpha mask too much as this
 * will cause a very ugly halo to appear around the window.
 *
 * A layered window is created by passing the WS_EX_LAYERED extended style to
 * the CreateWindowEx() function. The layered window will not be visible until
 * either a call to SetLayeredWindowAttributes() or UpdateLayeredWindow() is
 * made. This demo uses per pixel alpha blending to create non-rectangular
 * shaped windows so we call UpdateLayeredWindow() for this. See the code in the
 * InitLayeredWindow() function to see what is required.
 *
 * An important point to keep in mind when creating layered windows is that you
 * cannot create a layered window from a registered WNDCLASSEX with the class
 * styles: CS_OWNDC or CS_CLASSDC. Doing so will cause the CreateWindowEx()
 * call to fail.
 */ 

/* The layered window APIs are only visible if this is defined. */
#define _WIN32_WINNT 0x0500

/* Visual C++ 2005 includes a version of the CRT with security enhancements.
 * It deprecates much of the existing C library. To better support previous
 * versions of Visual C++ we will not use these updated APIs and instead use
 * the older (less safe) ones. This symbol will disable all warnings for using
 * the deprecated C API calls.
 */
#if (_MSC_VER >= 1400)
#define _CRT_SECURE_NO_DEPRECATE
#endif

/* Disable warning C4244: conversion from 'float' to 'BYTE', possible loss of
 * data. Used in the ImagePreMultAlpha() function.
 */
#pragma warning (disable : 4244)

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <crtdbg.h>

/* TGA file header structure. This *must* be byte aligned. */
#pragma pack(push, 1)
typedef struct
{
    BYTE idLength;
    BYTE colormapType;
    BYTE imageType;
    USHORT firstEntryIndex;
    USHORT colormapLength;
    BYTE colormapEntrySize;
    USHORT xOrigin;
    USHORT yOrigin;
    USHORT width;
    USHORT height;
    BYTE pixelDepth;
    BYTE imageDescriptor;
} TgaHeader;
#pragma pack(pop)

/* Generic wrapper around a DIB with a 32-bit color depth. */
typedef struct
{
    int width;
    int height;
    int pitch;
    HDC hdc;
    HBITMAP hBitmap;
    BITMAPINFO info;
    BYTE *pPixels;
} Image;

Image g_image;

void ImageDestroy(Image *pImage)
{
    if (!pImage)
        return;

    pImage->width = 0;
    pImage->height = 0;
    pImage->pitch = 0;

    if (pImage->hBitmap)
    {
        DeleteObject(pImage->hBitmap);
        pImage->hBitmap = NULL;
    }

    if (pImage->hdc)
    {
        DeleteDC(pImage->hdc);
        pImage->hdc = NULL;
    }

    memset(&pImage->info, 0, sizeof(pImage->info));
    pImage->pPixels = NULL;
}

BOOL ImageCreate(Image *pImage, int width, int height)
{
    /* All Windows DIBs are aligned to 4-byte (DWORD) memory boundaries. This
     * means that each scan line is padded with extra bytes to ensure that the
     * next scan line starts on a 4-byte memory boundary. The 'pitch' member
     * of the Image structure contains width of each scan line (in bytes).
     */

    if (!pImage)
        return FALSE;

    pImage->width = width;
    pImage->height = height;
    pImage->pitch = ((width * 32 + 31) & ~31) >> 3;
    pImage->pPixels = NULL;
    pImage->hdc = CreateCompatibleDC(NULL);
    
    if (!pImage->hdc)
        return FALSE;
    
    memset(&pImage->info, 0, sizeof(pImage->info));

    pImage->info.bmiHeader.biSize = sizeof(pImage->info.bmiHeader);
    pImage->info.bmiHeader.biBitCount = 32;
    pImage->info.bmiHeader.biWidth = width;
    pImage->info.bmiHeader.biHeight = -height;
    pImage->info.bmiHeader.biCompression = BI_RGB;
    pImage->info.bmiHeader.biPlanes = 1;

    pImage->hBitmap = CreateDIBSection(pImage->hdc, &pImage->info,
                        DIB_RGB_COLORS, (void**)&pImage->pPixels, NULL, 0);

    if (!pImage->hBitmap)
    {
        ImageDestroy(pImage);
        return FALSE;
    }

    GdiFlush();
    return TRUE;
}

void ImagePreMultAlpha(Image *pImage)
{
    /* The per pixel alpha blending API for layered windows deals with
     * pre-multiplied alpha values in the RGB channels. For further details see
     * the MSDN documentation for the BLENDFUNCTION structure. It basically
     * means we have to multiply each red, green, and blue channel in our image
     * with the alpha value divided by 255.
     *
     * Notes:
     * 1. ImagePreMultAlpha() needs to be called before every call to
     *    UpdateLayeredWindow().
     *
     * 2. Must divide by 255.0 instead of 255 to prevent alpha values in range
     *    [1, 254] from causing the pixel to become black. This will cause a
     *    conversion from 'float' to 'BYTE' possible loss of data warning which
     *    can be safely ignored.
     */

    BYTE *pPixel = NULL;

    if (pImage->width * 4 == pImage->pitch)
    {
        /* This is a special case. When the image width is already a multiple
         * of 4 the image does not require any padding bytes at the end of each
         * scan line. Consequently we do not need to address each scan line
         * separately. This is much faster than the below case where the image
         * width is not a multiple of 4.
         */

        int i = 0;
        int totalBytes = pImage->width * pImage->height * 4;

        for (i = 0; i < totalBytes; i += 4)
        {
            pPixel = &pImage->pPixels[i];
            pPixel[0] *= (BYTE)((float)pPixel[3] / 255.0f);
            pPixel[1] *= (BYTE)((float)pPixel[3] / 255.0f);
            pPixel[2] *= (BYTE)((float)pPixel[3] / 255.0f);
        }
    }
    else
    {
        /* Width of the image is not a multiple of 4. So padding bytes have
         * been included in the DIB's pixel data. Need to address each scan
         * line separately. This is much slower than the above case where the
         * width of the image is already a multiple of 4.
         */

        int x = 0;
        int y = 0;
        
        for (y = 0; y < pImage->height; ++y)
        {
            for (x = 0; x < pImage->width; ++x)
            {
                pPixel = &pImage->pPixels[(y * pImage->pitch) + (x * 4)];
                pPixel[0] *= (BYTE)((float)pPixel[3] / 255.0f);
                pPixel[1] *= (BYTE)((float)pPixel[3] / 255.0f);
                pPixel[2] *= (BYTE)((float)pPixel[3] / 255.0f);
            }
        }
    }
}

BOOL ImageImportTga(LPCSTR lpszFilename, Image *pImage)
{
    /* Load the 32-bit TGA file into our Image structure. Only true color
     * images with a 32 bit color depth and an alpha channel are supported.
     * Loading all other formats will fail.
     */

    int i = 0;
    int scanlineBytes = 0;
    BYTE *pRow = NULL;
    FILE *pFile = NULL;
    TgaHeader header = {0};

    if (!pImage)
        return FALSE;

    if (!(pFile = fopen(lpszFilename, "rb")))
        return FALSE;

    fread(&header, sizeof(TgaHeader), 1, pFile);

    /* Skip over the TGA image ID field. */
    if (header.idLength > 0)
        fseek(pFile, header.idLength, SEEK_CUR);

    if (header.pixelDepth != 32)
    {
        fclose(pFile);
        return FALSE;
    }
    
    if (!ImageCreate(pImage, header.width, header.height))
    {
        fclose(pFile);
        return FALSE;
    }

    if (header.imageType != 0x02)
    {
        ImageDestroy(pImage);
        fclose(pFile);
        return FALSE;
    }

    scanlineBytes = pImage->width * 4;

    if ((header.imageDescriptor & 0x30) == 0x20)
    {
        /* TGA is stored top down in file. */
        for (i = 0; i < pImage->height; ++i)
        {
            pRow = &pImage->pPixels[i * pImage->pitch];
            fread(pRow, scanlineBytes, 1, pFile);
        }
    }
    else
    {
        /* TGA is stored bottom up in file. */
        for (i = 0; i < pImage->height; ++i)
        {
            pRow = &pImage->pPixels[(pImage->height - 1 - i) * pImage->pitch];
            fread(pRow, scanlineBytes, 1, pFile);
        }
    }
    
    fclose(pFile);
    ImagePreMultAlpha(pImage);

    return TRUE;
}

void InitLayeredWindow(HWND hWnd)
{
    /* The call to UpdateLayeredWindow() is what makes a non-rectangular
     * window possible. To enable per pixel alpha blending we pass in the
     * argument ULW_ALPHA, and provide a BLENDFUNCTION structure filled in
     * to do per pixel alpha blending.
     */

    HDC hdc = NULL;
    
    if (hdc = GetDC(hWnd))
    {     
        HGDIOBJ hPrevObj = NULL;
        POINT ptDest = {0, 0};
        POINT ptSrc = {0, 0};
        SIZE client = {g_image.width, g_image.height};
        BLENDFUNCTION blendFunc = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        
        hPrevObj = SelectObject(g_image.hdc, g_image.hBitmap);
        ClientToScreen(hWnd, &ptDest);
        
        UpdateLayeredWindow(hWnd, hdc, &ptDest, &client, 
            g_image.hdc, &ptSrc, 0, &blendFunc, ULW_ALPHA);

        SelectObject(g_image.hdc, hPrevObj);
        ReleaseDC(hWnd, hdc);
    }
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            SendMessage(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;

    case WM_NCHITTEST:
        return HTCAPTION;   /* allows dragging of the window */

    default:
        break;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void DetectMemoryLeaks(void)
{
    /* Enable the memory leak tracking tools built into the CRT runtime. */

    _CrtSetDbgFlag(_CRTDBG_LEAK_CHECK_DF | _CRTDBG_ALLOC_MEM_DF);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    /* The WinMain() function uses SAL for its function parameters.
     * For more details on SAL see: https://docs.microsoft.com/en-us/cpp/code-quality/understanding-sal?view=msvc-170
     */

    WNDCLASSEX wcl = {0};
    
    DetectMemoryLeaks();

    wcl.cbSize = sizeof(wcl);
    wcl.style = CS_HREDRAW | CS_VREDRAW;
    wcl.lpfnWndProc = WindowProc;
    wcl.cbClsExtra = 0;
    wcl.cbWndExtra = 0;
    wcl.hInstance = hInstance;
    wcl.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcl.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcl.hbrBackground = NULL;
    wcl.lpszMenuName = NULL;
    wcl.lpszClassName = TEXT("LayeredWindowClass");
    wcl.hIconSm = NULL;

    if (!ImageImportTga("image.tga", &g_image))
    {
        MessageBox(0, TEXT("Failed to load \"image.tga\""),
            TEXT("Layered Window Demo"), MB_ICONSTOP);

        return 0;
    }    

    if (RegisterClassEx(&wcl))
    {
        HWND hWnd = CreateWindowEx(WS_EX_LAYERED, wcl.lpszClassName,
                        TEXT("Layered Window Demo"), WS_POPUP, 0, 0,
                        g_image.width, g_image.height, NULL, NULL,
                        wcl.hInstance, NULL);

        if (hWnd)
        {
            BOOL bRet = 0;
            MSG msg = {0};

            InitLayeredWindow(hWnd);
            ShowWindow(hWnd, nShowCmd);
            UpdateWindow(hWnd);
                        
            while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0)
            {
                if (bRet == -1)
                    break;

                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }        
        
        UnregisterClass(wcl.lpszClassName, hInstance);
    }

    ImageDestroy(&g_image);
    return 0;
}