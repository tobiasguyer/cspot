#include "Utils.h"

#include <stdlib.h>  // for strtol
#include <chrono>
#include <iomanip>      // for operator<<, setfill, setw
#include <iostream>     // for basic_ostream, hex
#include <sstream>      // for stringstream
#include <string>       // for string
#include <type_traits>  // for enable_if<>::type
#ifndef _WIN32
#include <arpa/inet.h>
#endif

static std::string Base62Alphabet =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
static char Base64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

unsigned long long getCurrentTimestamp() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

uint64_t hton64(uint64_t value) {
  int num = 42;
  if (*(char*)&num == 42) {
    uint32_t high_part = htonl((uint32_t)(value >> 32));
    uint32_t low_part = htonl((uint32_t)(value & 0xFFFFFFFFLL));
    return (((uint64_t)low_part) << 32) | high_part;
  } else {
    return value;
  }
}

std::string base64Encode(const std::vector<uint8_t>& v) {
  std::string ss;
  int nLenOut = 0;
  int index = 0;
  while (index < v.size()) {
    size_t decode_size = v.size() - index;
    unsigned long n = v[index];
    n <<= 8;
    n |= (decode_size > 1) ? v[index + 1] : 0;
    n <<= 8;
    n |= (decode_size > 2) ? v[index + 2] : 0;
    uint8_t m4 = n & 0x3f;
    n >>= 6;
    uint8_t m3 = n & 0x3f;
    n >>= 6;
    uint8_t m2 = n & 0x3f;
    n >>= 6;
    uint8_t m1 = n & 0x3f;

    ss.push_back(Base64Alphabet[m1]);
    ss.push_back(Base64Alphabet[m2]);
    ss.push_back(decode_size > 1 ? Base64Alphabet[m3] : '=');
    ss.push_back(decode_size > 2 ? Base64Alphabet[m4] : '=');
    index += 3;
  }
  return ss;
}

std::vector<uint8_t> stringHexToBytes(const std::string& s) {
  std::vector<uint8_t> v;
  v.reserve(s.length() / 2);

  for (std::string::size_type i = 0; i < s.length(); i += 2) {
    std::string byteString = s.substr(i, 2);
    uint8_t byte = (uint8_t)strtol(byteString.c_str(), NULL, 16);
    v.push_back(byte);
  }

  return v;
}

std::string bytesToHexString(const std::vector<uint8_t>& v) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  std::vector<uint8_t>::const_iterator it;

  for (it = v.begin(); it != v.end(); it++) {
    ss << std::setw(2) << static_cast<unsigned>(*it);
  }

  return ss.str();
}

std::vector<uint8_t> bigNumAdd(std::vector<uint8_t> num, int n) {
  auto carry = n;
  for (int x = num.size() - 1; x >= 0; x--) {
    int res = num[x] + carry;
    if (res < 256) {
      carry = 0;
      num[x] = res;
    } else {
      // Carry the rest of the division
      carry = res / 256;
      num[x] = res % 256;

      // extend the vector at the last index
      if (x == 0) {
        num.insert(num.begin(), carry);
        return num;
      }
    }
  }

  return num;
}

std::vector<uint8_t> bigNumDivide(std::vector<uint8_t> num, int n) {
  auto carry = 0;
  for (int x = 0; x < num.size(); x++) {
    int res = num[x] + carry * 256;
    if (res < n) {
      carry = res;
      num[x] = 0;
    } else {
      // Carry the rest of the division
      carry = res % n;
      num[x] = res / n;
    }
  }

  return num;
}

std::vector<uint8_t> bigNumMultiply(std::vector<uint8_t> num, int n) {
  auto carry = 0;
  for (int x = num.size() - 1; x >= 0; x--) {
    int res = num[x] * n + carry;
    if (res < 256) {
      carry = 0;
      num[x] = res;
    } else {
      // Carry the rest of the division
      carry = res / 256;
      num[x] = res % 256;

      // extend the vector at the last index
      if (x == 0) {
        num.insert(num.begin(), carry);
        return num;
      }
    }
  }

  return num;
}
unsigned char h2int(char c) {
  if (c >= '0' && c <= '9') {
    return ((unsigned char)c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return ((unsigned char)c - 'a' + 10);
  }
  if (c >= 'A' && c <= 'F') {
    return ((unsigned char)c - 'A' + 10);
  }
  return (0);
}

std::string urlDecode(std::string str) {
  std::string encodedString = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++) {
    c = str[i];
    if (c == '+') {
      encodedString += ' ';
    } else if (c == '%') {
      i++;
      code0 = str[i];
      i++;
      code1 = str[i];
      c = (h2int(code0) << 4) | h2int(code1);
      encodedString += c;
    } else {

      encodedString += c;
    }
  }

  return encodedString;
}

std::pair<SpotifyFileType, std::vector<uint8_t>> base62Decode(std::string uri) {
  std::vector<uint8_t> n = std::vector<uint8_t>({0});
  SpotifyFileType type = SpotifyFileType::UNKNOWN;
  auto it = uri.begin();
  if (uri.find(":") != std::string::npos) {
    if (uri.find("episode:") != std::string::npos) {
      type = SpotifyFileType::EPISODE;
    } else if (uri.find("track:") != std::string::npos) {
      type = SpotifyFileType::TRACK;
    }
    it += uri.rfind(":") + 1;
  }
  while (it != uri.end()) {
    size_t d = Base62Alphabet.find(*it);
    n = bigNumMultiply(n, 62);
    n = bigNumAdd(n, d);
    it++;
  }

  return std::make_pair(type, n);
}