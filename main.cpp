#include <windows.h>
#include <urlmon.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include <comdef.h>
#include <Wbemidl.h>
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "wbemuuid.lib")

#include <dxgi.h>         // for IDXGIFactory, IDXGIAdapter, DXGI_ADAPTER_DESC
#include <dxgi1_2.h>      // (optional, but good for later versions)
#include <d3d11.h>        // (optional, for Direct3D 11 interfaces if needed)
#pragma comment(lib, "dxgi.lib")  // link dxgi.lib

namespace fs = std::filesystem;

IMAGE_DOS_HEADER* dosHeader = nullptr;
IMAGE_NT_HEADERS32* ntHeaders = nullptr;
BYTE* base = nullptr;


// Helper: Download file via URLDownloadToFile
bool DownloadFile(const std::wstring& url, const fs::path& outputPath) {
    std::wcout << L"Downloading " << url << L" to " << outputPath.wstring() << L" ...\n";
    HRESULT hr = URLDownloadToFileW(nullptr, url.c_str(), outputPath.c_str(), 0, nullptr);
    if (FAILED(hr)) {
        std::wcerr << L"Failed to download " << url << L"\n";
        return false;
    }
    return true;
}
bool SetLargeAddressAware(const fs::path& exePath) {
    std::wcout << L"Setting LARGE_ADDRESS_AWARE flag in " << exePath.wstring() << L"\n";
    HANDLE hFile = CreateFileW(exePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        std::wcerr << L"Failed to open file for modification.\n";
        return false;
    }

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        std::wcerr << L"Failed to get file size.\n";
        return false;
    }

    HANDLE hMap = CreateFileMappingW(hFile, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    if (!hMap) {
        CloseHandle(hFile);
        std::wcerr << L"Failed to create file mapping.\n";
        return false;
    }

    LPVOID pMap = MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, 0);
    if (!pMap) {
        CloseHandle(hMap);
        CloseHandle(hFile);
        std::wcerr << L"Failed to map view of file.\n";
        return false;
    }

    BYTE* base = (BYTE*)pMap;

    // Declare these at top to avoid bypass by goto
    IMAGE_DOS_HEADER* dosHeader = nullptr;
    IMAGE_NT_HEADERS32* ntHeaders = nullptr;

    // DOS header check
    dosHeader = (IMAGE_DOS_HEADER*)base;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        std::wcerr << L"Invalid DOS signature.\n";
        goto cleanup_fail;
    }

    // PE header
    ntHeaders = (IMAGE_NT_HEADERS32*)(base + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        std::wcerr << L"Invalid PE signature.\n";
        goto cleanup_fail;
    }

    // Check if 32-bit
    if (ntHeaders->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
        std::wcerr << L"Not a 32-bit executable. Skipping LARGE_ADDRESS_AWARE flag.\n";
        goto cleanup_fail;
    }

    // Set flag
    if ((ntHeaders->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) == 0) {
        ntHeaders->FileHeader.Characteristics |= IMAGE_FILE_LARGE_ADDRESS_AWARE;
        std::wcout << L"Flag set successfully.\n";
    }
    else {
        std::wcout << L"Flag already set.\n";
    }

    FlushViewOfFile(pMap, 0);

    UnmapViewOfFile(pMap);
    CloseHandle(hMap);
    CloseHandle(hFile);
    return true;

cleanup_fail:
    UnmapViewOfFile(pMap);
    CloseHandle(hMap);
    CloseHandle(hFile);
    return false;
}


// Run a command and wait for it to finish
bool RunCommandAndWait(const std::wstring& cmd) {
    std::wcout << L"Executing: " << cmd << L"\n";

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    if (!CreateProcessW(nullptr, (LPWSTR)cmd.c_str(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        std::wcerr << L"Failed to execute command.\n";
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0) {
        std::wcerr << L"Command exited with code " << exitCode << L"\n";
        return false;
    }

    return true;
}

// Extract ZIP archive with powershell
bool ExtractZip(const fs::path& zipPath, const fs::path& destDir) {
    std::wstring cmd = L"powershell -Command \"Expand-Archive -LiteralPath '" + zipPath.wstring() + L"' -DestinationPath '" + destDir.wstring() + L"' -Force\"";
    return RunCommandAndWait(cmd);
}

// Extract tar.gz archive with powershell (needs at least Windows 10)
bool ExtractTarGz(const fs::path& archivePath, const fs::path& destDir) {
    // Powershell 5+ has tar alias, but tar.exe can be used
    std::wstring cmd = L"powershell -Command \"tar -xzf '" + archivePath.wstring() + L"' -C '" + destDir.wstring() + L"'\"";
    return RunCommandAndWait(cmd);
}

// Detect if GPU vendor is AMD by querying DXGI
bool IsAMD() {
    bool isAMD = false;
    IDXGIFactory* pFactory = nullptr;
    if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory))) {
        std::wcerr << L"Failed to create DXGIFactory.\n";
        return false;
    }

    IDXGIAdapter* pAdapter = nullptr;
    for (UINT i = 0; pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC desc;
        if (SUCCEEDED(pAdapter->GetDesc(&desc))) {
            std::wstring descStr(desc.Description);
            if (descStr.find(L"AMD") != std::wstring::npos || descStr.find(L"Radeon") != std::wstring::npos) {
                isAMD = true;
                pAdapter->Release();
                break;
            }
        }
        pAdapter->Release();
    }
    pFactory->Release();
    return isAMD;
}

int wmain() {
    // Initialize COM for WMI / DXGI
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::wcout << L"Enter path to Gothic directory: ";
    std::wstring gothicDir;
    std::getline(std::wcin, gothicDir);

    fs::path gothicPath(gothicDir);
    if (!fs::exists(gothicPath) || !fs::is_directory(gothicPath)) {
        std::wcerr << L"Invalid directory.\n";
        return 1;
    }

    // Download steps
    struct DownloadItem {
        std::wstring url;
        std::wstring filename;
    };

    std::vector<DownloadItem> downloads = {
        {L"https://www.worldofgothic.de/download.php?id=15", L"gothic_patch_108k.exe"},
        {L"https://www.worldofgothic.de/download.php?id=61", L"gothic1_playerkit-1.08k.exe"},
        {L"https://www.worldofgothic.de/download.php?id=1523", L"G1Classic-SystemPack-1.8.exe"},
        {L"https://github.com/kirides/GD3D11/releases/download/v17.8-dev26/GD3D11-v17.8-dev26.zip", L"GD3D11-v17.8-dev26.zip"}
    };

    for (const auto& item : downloads) {
        fs::path outFile = gothicPath / item.filename;

        if (!DownloadFile(item.url, outFile)) {
            std::wcerr << L"Download failed. Exiting.\n";
            return 1;
        }

        if (outFile.extension() == L".exe") {
            std::wcout << L"Running installer " << outFile.wstring() << L"\n";
            if (!RunCommandAndWait(outFile.wstring())) {
                std::wcerr << L"Failed to run " << outFile.wstring() << L"\n";
                return 1;
            }

            try {
                fs::remove(outFile);
                std::wcout << L"Deleted installer " << outFile.wstring() << L"\n";
            }
            catch (const fs::filesystem_error& e) {
                std::wcerr << L"Failed to delete " << outFile.wstring() << L": " << e.what() << L"\n";
            }
        }
    }


    // Step 4: Set LARGE_ADDRESS_AWARE flag in gothic/system/GOTHIC.exe
    fs::path gothicExe = gothicPath / L"system" / L"GOTHIC.exe";
    if (!fs::exists(gothicExe)) {
        std::wcerr << L"GOTHIC.exe not found in system directory.\n";
        return 1;
    }

    if (!SetLargeAddressAware(gothicExe)) {
        std::wcerr << L"Failed to set LARGE_ADDRESS_AWARE flag.\n";
        // not fatal, continue
    }

    // Step 5: Extract GD3D11 zip to gothic/system
    fs::path gd3d11Zip = gothicPath / L"GD3D11-v17.8-dev26.zip";
    fs::path systemDir = gothicPath / L"system";

    if (!ExtractZip(gd3d11Zip, systemDir)) {
        std::wcerr << L"Failed to extract GD3D11.\n";
        return 1;
    }

    try {
        fs::remove(gd3d11Zip);
        std::wcout << L"Deleted archive " << gd3d11Zip.wstring() << L"\n";
    }
    catch (const fs::filesystem_error& e) {
        std::wcerr << L"Failed to delete " << gd3d11Zip.wstring() << L": " << e.what() << L"\n";
    }


    // Step 6: Detect AMD, if yes, download and extract dxvk
    if (IsAMD()) {
        std::wcout << L"AMD GPU detected, downloading DXVK...\n";
        std::wstring dxvkUrl = L"https://github.com/doitsujin/dxvk/releases/download/v2.6.2/dxvk-2.6.2.tar.gz";
        fs::path dxvkArchive = gothicPath / L"dxvk-2.6.2.tar.gz";

        if (!DownloadFile(dxvkUrl, dxvkArchive)) {
            std::wcerr << L"Failed to download DXVK archive.\n";
            return 1;
        }

        if (!ExtractTarGz(dxvkArchive, systemDir)) {
            std::wcerr << L"Failed to extract DXVK archive.\n";
            return 1;
        }

        try {
            fs::remove(dxvkArchive);
            std::wcout << L"Deleted archive " << dxvkArchive.wstring() << L"\n";
        }
        catch (const fs::filesystem_error& e) {
            std::wcerr << L"Failed to delete " << dxvkArchive.wstring() << L": " << e.what() << L"\n";
        }

    } else {
        std::wcout << L"Non-AMD GPU detected, skipping DXVK.\n";
    }

    std::wcout << L"All done.\n";

    CoUninitialize();
    return 0;
}
