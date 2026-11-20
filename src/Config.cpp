// SPDX-License-Identifier: GPL-3.0-only
#include "Settings.h"
#include "DefaultIni.h"
#include <cstdio>
#include <windows.h>

namespace locks {
static std::wstring settingsPath(const std::wstring &folder) {
  return folder + L"\\TTWVRPhysicalLockpicking.ini";
}

Ergonomics LoadPlayerSettings(const std::wstring &folder) {
  Ergonomics settings;
  FILE *file = _wfopen(settingsPath(folder).c_str(), L"rb");
  if (!file) {
    settings.load(defaultIni);
    return settings;
  }
  std::string text;
  char buffer[4096];
  while (size_t count = fread(buffer, 1, sizeof(buffer), file))
    text.append(buffer, count);
  const bool failed = ferror(file) != 0;
  fclose(file);
  settings.load(failed ? defaultIni : text);
  return settings;
}

// Publish a complete file without replacing an existing INI, including one
// created by another process between the existence check and the rename.
DWORD EnsureDefaultSettings(const std::wstring &folder) {
  const auto path = settingsPath(folder);
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES)
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? ERROR_DIRECTORY : ERROR_ALREADY_EXISTS;
  DWORD error = GetLastError();
  if (error != ERROR_FILE_NOT_FOUND) return error;
  const auto temp = path + L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
                    std::to_wstring(GetTickCount64()) + L".tmp";
  HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return GetLastError();
  DWORD written = 0;
  const DWORD size = sizeof(defaultIni) - 1;
  error = ERROR_SUCCESS;
  if (!WriteFile(file, defaultIni, size, &written, nullptr))
    error = GetLastError();
  else if (written != size)
    error = ERROR_WRITE_FAULT;
  if (!CloseHandle(file) && error == ERROR_SUCCESS) error = GetLastError();
  if (error == ERROR_SUCCESS) {
    if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH)) return ERROR_SUCCESS;
    error = GetLastError();
    if (error == ERROR_FILE_EXISTS) error = ERROR_ALREADY_EXISTS;
  }
  DeleteFileW(temp.c_str());
  return error;
}
} // namespace locks
