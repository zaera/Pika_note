#pragma once
#ifdef PIKA_PHONE_MODE
void pikaPhoneFileTouched(const char* path);
bool pikaPhoneFileWillChange(const char* path);
#endif

template <typename FileSystem, typename Path>
bool recoverStoredFile(FileSystem& fs, const Path& path) {
  if (fs.exists(path.c_str())) return true;
  const Path backup = path + ".bak";
  return fs.exists(backup.c_str()) && fs.rename(backup.c_str(), path.c_str());
}

template <typename FileSystem, typename Path>
bool commitStoredFile(FileSystem& fs, const Path& path, const Path& temp) {
  if (!fs.exists(temp.c_str())) return false;
#ifdef PIKA_PHONE_MODE
  if(!pikaPhoneFileWillChange(path.c_str()))return false;
#endif
  const Path backup = path + ".bak";
  if (fs.exists(backup.c_str())) {
    if (!fs.exists(path.c_str())) {
      if (!fs.rename(backup.c_str(), path.c_str())) return false;
    } else if (!fs.remove(backup.c_str())) return false;
  }
  if (fs.exists(path.c_str()) && !fs.rename(path.c_str(), backup.c_str())) return false;
  if (fs.rename(temp.c_str(), path.c_str())) {
    fs.remove(backup.c_str());
#ifdef PIKA_PHONE_MODE
    pikaPhoneFileTouched(path.c_str());
#endif
    return true;
  }
  recoverStoredFile(fs, path);
  return false;
}
