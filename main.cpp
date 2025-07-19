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

// Extract ZIP archive with PowerShell and only overwrite matching files
bool ExtractZip(const fs::path& zipPath, const fs::path& destDir) {
    std::wstring cmd =
        L"powershell -Command \""
        L"Expand-Archive -LiteralPath '" + zipPath.wstring() + L"' -DestinationPath '" + destDir.wstring() + L"' -Force"
        L"\"";
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
    #pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")


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

    bool isGothic2 = gothicPath.filename().wstring().find(L"Gothic II") != std::wstring::npos;
    std::vector<DownloadItem> downloads;

    if (isGothic2) {
        std::wcout << L"Gothic II detected.\n";

        downloads = {
            {L"https://www.worldofgothic.de/download.php?id=833", L"gothic2_fix-2.6.0.0-rev2.exe"},
            {L"https://www.worldofgothic.de/download.php?id=518", L"gothic2_playerkit-2.6f.exe"},
            {L"https://www.worldofgothic.de/download.php?id=1525", L"G2NoTR-SystemPack-1.8.exe"},
            {L"https://github.com/GothicFixTeam/GothicFix/releases/download/v1.9pre2/Vdfs32g.zip", L"Vdfs32g.zip"},
            {L"https://github.com/kirides/GD3D11/releases/download/v17.8-dev26/GD3D11-v17.8-dev26.zip", L"GD3D11-v17.8-dev26.zip"},
            {L"https://www.worldofgothic.de/download.php?id=1509", L"Normalmaps_Original.zip"},
            {L"https://github.com/rockysx27/carnagec/releases/download/g2/FCH_Models_G2.vdf", L"FCH_Models_G2.vdf"}

        };
    } else {
        std::wcout << L"Gothic I detected.\n";

        downloads = {
            {L"https://www.worldofgothic.de/download.php?id=15", L"gothic_patch_108k.exe"},
            {L"https://www.worldofgothic.de/download.php?id=61", L"gothic1_playerkit-1.08k.exe"},
            {L"https://www.worldofgothic.de/download.php?id=1523", L"G1Classic-SystemPack-1.8.exe"},
            {L"https://github.com/GothicFixTeam/GothicFix/releases/download/v1.9pre2/Vdfs32g.zip", L"Vdfs32g.zip"},
            {L"https://github.com/kirides/GD3D11/releases/download/v17.8-dev26/GD3D11-v17.8-dev26.zip", L"GD3D11-v17.8-dev26.zip"},
            {L"https://github.com/rockysx27/carnagec/releases/download/g1/FCH_Models_G1.vdf", L"FCH_Models_G1.vdf"}
        };
    }

    for (const auto& item : downloads) {
        fs::path outFile = gothicPath / item.filename;

        if (!DownloadFile(item.url, outFile)) {
            std::wcerr << L"Download failed. Exiting.\n";
            return 1;
        }

        if (outFile.extension() == L".exe") {
            std::wcout << L"Running installer " << outFile.wstring() << L"\n" << std::flush;

            // Run installer synchronously and wait for it to finish
            if (!RunCommandAndWait(outFile.wstring())) {
                std::wcerr << L"Failed to run " << outFile.wstring() << L"\n";
                return 1;
            }

            std::wcout << L"Installer finished, deleting file...\n" << std::flush;

            try {
                fs::remove(outFile);
                std::wcout << L"Deleted installer " << outFile.wstring() << L"\n";
            }
            catch (const fs::filesystem_error& e) {
                std::wcerr << L"Failed to delete " << outFile.wstring() << L": " << e.what() << L"\n";
            }
        }
    }

    //ADD CARNAGE MODELS
    // Move Carnage VDF into Data directory
    fs::path carnageVdf = gothicPath / (isGothic2 ? L"FCH_Models_G2.vdf" : L"FCH_Models_G1.VDF");
    fs::path dataDir = gothicPath / L"Data";
    fs::path targetVdf = dataDir / carnageVdf.filename();

    try {
        if (!fs::exists(dataDir)) {
            fs::create_directory(dataDir);
        }

        fs::rename(carnageVdf, targetVdf);
        std::wcout << L"Moved " << carnageVdf.filename().wstring() << L" to " << targetVdf.wstring() << L"\n";
    } catch (const fs::filesystem_error& e) {
        std::wcerr << L"Failed to move VDF file: " << e.what() << L"\n";
        // Not fatal
    }
	// Step 3.5: Extract Vdfs32g zip to gothic/system
    fs::path replaceZipX = gothicPath / L"Vdfs32g.zip";
    fs::path systemReplaceDirX = gothicPath / L"system";

    if (!ExtractZip(replaceZipX, systemReplaceDirX)) {
        std::wcerr << L"Failed to extract Vdfs32g.\n";
        return 1;
    }

    try {
        fs::remove(replaceZipX);
        std::wcout << L"Deleted archive " << replaceZipX.wstring() << L"\n";
    }
    catch (const fs::filesystem_error& e) {
        std::wcerr << L"Failed to delete " << replaceZipX.wstring() << L": " << e.what() << L"\n";
    }

    // Step 4: Set LARGE_ADDRESS_AWARE flag in gothic/system/{GOTHIC.exe | Gothic2.exe}
    fs::path gothicExe = isGothic2
        ? gothicPath / L"system" / L"Gothic2.exe"
        : gothicPath / L"system" / L"GOTHIC.exe";

    if (!fs::exists(gothicExe)) {
        std::wcerr << (isGothic2 ? L"Gothic2.exe" : L"GOTHIC.exe") << L" not found in system directory.\n";
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

    //If GOTHIC 2 ADD REPLACEMENT MESHES for GD3D11
    if (isGothic2) {
        // Step 5.5: Extract maps zip to system\GD3D11\textures\replacements
        fs::path replaceZip = gothicPath / L"Normalmaps_Original.zip";
        fs::path systemReplaceDir = gothicPath / L"system/GD3D11/textures/replacements";

         if (!ExtractZip(replaceZip, systemReplaceDir)) {
            std::wcerr << L"Failed to extract Normalmaps_Original.\n";
            return 1;
        }

        try {
            fs::remove(replaceZip);
            std::wcout << L"Deleted archive " << replaceZip.wstring() << L"\n";
        }
        catch (const fs::filesystem_error& e) {
            std::wcerr << L"Failed to delete " << replaceZip.wstring() << L": " << e.what() << L"\n";
        }
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

        // After extracting the tar.gz archive...
        if (!ExtractTarGz(dxvkArchive, systemDir)) {
            std::wcerr << L"Failed to extract DXVK archive.\n";
            return 1;
        }

        // Construct path to the extracted folder and the x32 subfolder
        fs::path extractedDir = systemDir / L"dxvk-2.6.2" / L"x32";

        // Files to copy
        std::vector<fs::path> dllFiles = {
            extractedDir / L"d3d11.dll",
            extractedDir / L"dxgi.dll"
        };

        for (const auto& dllFile : dllFiles) {
            fs::path destFile = systemDir / dllFile.filename();
            try {
                fs::copy_file(dllFile, destFile, fs::copy_options::overwrite_existing);
                std::wcout << L"Copied " << dllFile.filename().wstring() << L" to " << destFile.wstring() << L"\n";
            }
            catch (const fs::filesystem_error& e) {
                std::wcerr << L"Failed to copy " << dllFile.filename().wstring() << L": " << e.what() << L"\n";
                return 1;
            }
        }

        // Clean up extracted folder if you want
        try {
            fs::remove_all(systemDir / L"dxvk-2.6.2");
            std::wcout << L"Deleted extracted dxvk folder.\n";
        }
        catch (const fs::filesystem_error& e) {
            std::wcerr << L"Failed to delete extracted folder: " << e.what() << L"\n";
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
