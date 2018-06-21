/*
Copyright (C) 2018 Adrian Michel
http://www.amichel.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "stdafx.h"

#include <stdio.h>
#include <string.h>

#include <curl/curl.h>
#include <curl/easy.h>

#include <boost/shared_ptr.hpp>

using namespace tradery;

#include <strings.h>
#include <HttpRequest.h>

CHTTPRequest::CHTTPRequest(const std::wstring& serverName,
                           const std::wstring& doc)
    : m_doc(doc),
      HTTPRequestBase(serverName),
      _response(new std::wstring),
      _cancelling(false) {
  m_hSession = CHInternet(InternetOpen(
      _T( "Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 1.1.4322; .NET CLR 2.0.50727)" ),
      INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0));
  if (!(bool)m_hSession)
    // Internet open error
    throw InternetException(InternetException::ErrorType::INTERNET_OPEN_ERROR);

  m_hConnect = CHInternet(InternetConnect(
      m_hSession, __super::getServerName().c_str(), INTERNET_DEFAULT_HTTP_PORT,
      NULL, NULL, INTERNET_SERVICE_HTTP,
      INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_CACHE_WRITE, NULL));
  if (!(bool)m_hConnect)
    // Failed to connect
    throw InternetException(InternetException::ErrorType::FAILED_TO_CONNECT);
}

tradery::StringPtr CHTTPRequest::get(
    const std::wstring& object, const std::wstring& extraHeaders,
    const std::wstring& userAgent, const std::wstring& proxyServerAddress,
    const std::wstring& proxyServerUserName,
    const std::wstring& proxyServerPassword,
    unsigned int timeout) throw(InternetException) {
  if (cancelling()) {
    resetCancel();
    throw InternetException(InternetException::ErrorType::REQUEST_CANCELLED);
  }
  try {
    _response->clear();
    CHInternet hRequest(HttpOpenRequest(
        m_hConnect, _T( "GET" ), object.c_str(), NULL, NULL, NULL,
        INTERNET_FLAG_KEEP_CONNECTION | INTERNET_FLAG_NO_CACHE_WRITE, 0));
    if (!(bool)hRequest)
      throw InternetException(
          InternetException::ErrorType::HTTP_OPEN_REQUEST_ERROR);
    else {
      HttpAddRequestHeaders(hRequest, extraHeaders.c_str(), -1L,
                            HTTP_ADDREQ_FLAG_ADD);
      {
        UseHttpSendReqEx(hRequest);
        DWORD code;
        DWORD size = sizeof(DWORD);
        if (!HttpQueryInfo((HINTERNET)hRequest,
                           HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                           &code, &size, 0)) {
          code = GetLastError();
          throw InternetException(
              InternetException::ErrorType::HTTP_QUERY_INFO_ERROR);
        }

        switch (code) {
          case 200:
            break;
          case 400:
          case 404:
            throw InternetException(
                InternetException::ErrorType::HTTP_RESPONSE_ERROR, code);
          default:
            break;
        }

        TCHAR pcBuffer[BUFFSIZE];
        DWORD dwBytesRead;

        // reading the server response
        do {
          dwBytesRead = 0;
          if (InternetReadFile(hRequest, pcBuffer, BUFFSIZE - 1, &dwBytesRead))
            pcBuffer[dwBytesRead] = 0;  // Null-terminate buffer
          else
            throw InternetException(
                InternetException::ErrorType::INTERNET_READ_FILE_ERROR);

          *_response += pcBuffer;
        } while (dwBytesRead > 0);
      }

      /*                else
              {
                      DWORD dw = GetLastError();
                      throw InternetException(
         InternetException::ErrorType::HTTP_ADD_REQUEST_ERROR );
              }*/
    }

    assert(_response);
    return StringPtr(new std::string(tradery::ws2s(*_response)));
  } catch (const CHandleException&) {
    throw InternetException(
        InternetException::ErrorType::FAILED_TO_CLOSE_INTERNET_HANDLE);
  }
}

void CHTTPRequest::UseHttpSendReqEx(HINTERNET hRequest) const
    throw(InternetException) {
  DWORD dwPostSize = m_doc.length();
  INTERNET_BUFFERS BufferIn;

  BufferIn.dwStructSize =
      sizeof(INTERNET_BUFFERS);  // Must be set or error will occur
  BufferIn.Next = NULL;
  BufferIn.lpcszHeader = NULL;
  BufferIn.dwHeadersLength = 0;
  BufferIn.dwHeadersTotal = 0;
  BufferIn.lpvBuffer = 0;
  BufferIn.dwBufferLength = 0;
  BufferIn.dwBufferTotal =
      dwPostSize;  // This is the only member used other than dwStructSize
  BufferIn.dwOffsetLow = 0;
  BufferIn.dwOffsetHigh = 0;

  try {
    if (!HttpSendRequestEx(hRequest, &BufferIn, NULL, 0, HSR_ASYNC)) {
      DWORD le = GetLastError();
      throw InternetException(
          InternetException::ErrorType::HTTP_SEND_REQUEST_EX_ERROR);
    }
  } catch (...) {
    DWORD le = GetLastError();
    throw InternetException(
        InternetException::ErrorType::HTTP_SEND_REQUEST_EX_ERROR);
  }

  if (!HttpEndRequest(hRequest, NULL, 0, 0))
    throw InternetException(
        InternetException::ErrorType::HTTP_END_REQUEST_ERROR);
}

class ResponseString {
 private:
  std::string _str;

 public:
  void append(char* s, size_t size) { _str.append(s, size); }

  operator std::string() { return _str; }
};

static size_t responseCallback(void* ptr, size_t size, size_t nmemb,
                               void* data) {
  int realsize = size * nmemb;
  ResponseString* rs = static_cast<ResponseString*>(data);
  rs->append((char*)ptr, realsize);
  return realsize;
}

int CurlHTTPRequest::xprogress(void* req, double t, double d, double ultotal,
                               double ulnow) {
  //	OutputDebugString( "curl request progress\n" );
  CurlHTTPRequest* p = static_cast<CurlHTTPRequest*>(req);
  return p->progress();
}

void CurlHTTPRequest::cancel() {
  Lock lock(_mx);
  _cancel = true;
}

void CurlHTTPRequest::unCancel() {
  tradery::Lock lock(_mx);
  _cancel = false;
  _canceled = false;
}

int CurlHTTPRequest::progress() {
  Lock lock(_mx);
  if (_cancel) {
    _cancel = false;
    _canceled = true;
    return 1;
  } else
    return 0;
}

// encapsulation of the formpost struct and code
class FormPost {
 private:
  curl_httppost* _formpost;
  curl_httppost* _lastptr;
  void* _curl;

 private:
  curl_httppost** formpost() { return &_formpost; }
  curl_httppost** lastptr() { return &_lastptr; }

  void add(const NameValue& nv) {
    boost::shared_ptr<char> str(
        curl_easy_escape(_curl, nv.value().c_str(), nv.value().length()),
        curl_free);
    curl_formadd(formpost(), lastptr(), CURLFORM_COPYNAME, nv.name().c_str(),
                 CURLFORM_COPYCONTENTS, str.get(), CURLFORM_END);
  }

  void add(const NameValueMap& data) {
    for (NameValueMap::size_type n = 0; n < data.size(); ++n) add(data[n]);
  }

 public:
  FormPost(const NameValueMap& data, void* curl)
      : _formpost(0), _lastptr(0), _curl(curl) {
    assert(curl != 0);

    add(data);
  }

  ~FormPost() { curl_formfree(_formpost); }

  operator curl_httppost*() { return _formpost; }
};

void CurlHTTPRequest::setVerify() {
  if (!m_verifyPeer)
    handleCurlResult(curl_easy_setopt(_curl, CURLOPT_SSL_VERIFYPEER, 0L),
                     "curl_easy_setopt( CURLOPT_SSL_VERIFYPEER )");

  if (!m_verifyHost)
    handleCurlResult(curl_easy_setopt(_curl, CURLOPT_SSL_VERIFYHOST, 0L),
                     "curl_easy_setopt( CURLOPT_SSL_VERIFYHOST )");
}

std::wstring HTTPRequestBase::url(const std::wstring& object) {
  return protocol() << _T( "://" ) + getServerName() + object;
}

tradery::StringPtr CurlHTTPRequest::post(
    const std::wstring& object, const NameValueMap& data,
    const std::wstring& extraHeaders, const std::wstring& proxyServerAddress,
    const std::wstring& proxyServerUserName,
    const std::wstring& proxyServerPassword, unsigned int timeout) {
  LOG(log_debug, "data: " + *data.toString());
  curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);

  if (info->features & CURL_VERSION_ASYNCHDNS)
    OutputDebugString(_T( "ares enabled\n"));
  else
    OutputDebugString(_T( "ares NOT enabled\n"));

  /* First set the URL that is about to receive our POST. This URL can
     just as well be a https:// URL if that is what should receive the
     data. */

  ResponseString rs;  // for the response
  ResponseString hs;  // for the header

  /*	struct curl_httppost *formpost=NULL;
          struct curl_httppost *lastptr=NULL;
          */

  FormPost fp(data, _curl);

  //	tradery::ManagedPtr< curl_httppost* > x( &formpost, cleanformpost );

  std::wstring url(url(object));
  handleCurlResult(curl_easy_setopt(_curl, CURLOPT_URL, url.c_str()),
                   "curl_easy_setopt( CURLOPT_URL )");

  /* Now specify the POST data */
  //	handleCurlResult( curl_easy_setopt( _curl, CURLOPT_POSTFIELDS,
  //"name=daniel&project=curl"), "curly_easy_setopt( CURLOPT_POSTFIELDS )" );

  setVerify();

  handleCurlResult(
      curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, responseCallback),
      "curl_easy_setopt( CURLOPT_WRITEFUNCTION )");
  handleCurlResult(curl_easy_setopt(_curl, CURLOPT_FILE, (void*)&rs),
                   "curl_easy_setopt( CURLOPT_FILE )");
  handleCurlResult(curl_easy_setopt(_curl, CURLOPT_NOPROGRESS, 0L),
                   "curl_easy_setopt( CURLOPT_NOPROGRESS )");
  handleCurlResult(curl_easy_setopt(_curl, CURLOPT_PROGRESSFUNCTION, xprogress),
                   "curl_easy_setopt( CURLOPT_PROGRESSFUNCTION )");
  handleCurlResult(curl_easy_setopt(_curl, CURLOPT_PROGRESSDATA, this),
                   "curl_easy_setopt( CURLOPT_PROGRESSDATA )");
  // this sets the level of verbosity
  //	handleCurlResult( curl_easy_setopt( _curl, CURLOPT_VERBOSE, 1),
  //"curl_easy_setopt( CURLOPT_VERBOSE )" );

  //	boost::shared_ptr< FILE > curllog( fopen( "c:\\dev\\curl.log", "a" ),
  // fclose ); 	handleCurlResult( curl_easy_setopt( _curl, CURLOPT_STDERR,
  // curllog.get()), "curl_easy_setopt( CURLOPT_STDERR )" );

  handleCurlResult(curl_easy_setopt(_curl, CURLOPT_HTTPPOST, fp),
                   "curl_easy_setopt( CURLOPT_HTTPPOST )");
  handleCurlResult(
      curl_easy_setopt(_curl, CURLOPT_PROXY, proxyServerAddress.c_str()),
      "curl_easy_setup( CURLOPT_PROXY )");

  handleCurlResult(curl_easy_setopt(_curl, CURLOPT_PROXYUSERNAME,
                                    proxyServerUserName.c_str()),
                   "curl_easy_setup( CURLOPT_PROXYUSERNAME )");
  handleCurlResult(curl_easy_setopt(_curl, CURLOPT_PROXYPASSWORD,
                                    proxyServerPassword.c_str()),
                   "curl_easy_setup( CURLOPT_PROXYPASSWORD )");

  // set a timeout of 40 seconds - should be enough. What is the default
  // timeout?
  if (timeout > 0)
    handleCurlResult(curl_easy_setopt(_curl, CURLOPT_TIMEOUT, timeout),
                     "curl_easy_setopt( CURLOPT_TIMEOUT )");

  handleCurlResult(
      curl_easy_setopt(_curl, CURLOPT_USERAGENT, "libcurl-agent/1.0"),
      "curl_easy_setopt( CURLOPT_USERAGENT )");
  /* Perform the request, res will get the return code */
  handleCurlResult(curl_easy_perform(_curl), "curl_easy_perform");

  long responseCode;

  curl_easy_getinfo(_curl, CURLINFO_RESPONSE_CODE, &responseCode);

  LOG(log_debug, "curl response code: " << responseCode);

  if (_canceled)
    throw InternetException(InternetException::REQUEST_CANCELLED, -1);
  /* always cleanup */

  if (responseCode != 200 && responseCode != 0)
    throw InternetException(InternetException::HTTP_RESPONSE_ERROR,
                            responseCode);

  return tradery::StringPtr(new std::string(rs));
}

tradery::StringPtr CurlHTTPRequest::get(const std::wstring& object,
                                        const std::wstring& extraHeaders,
                                        const std::wstring& userAgent,
                                        const std::wstring& proxyServerAddress,
                                        const std::wstring& proxyServerUserName,
                                        const std::wstring& proxyServerPassword,
                                        unsigned int timeout) {
  ResponseString rs;  // for the response
  ResponseString hs;  // for the header

  handleCurlResult(curl_easy_setopt(_curl, CURLOPT_URL, url(object).c_str()),
                   "curl_easy_setopt( CURLOPT_URL )");

  setVerify();

  handleCurlResult(
      curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, responseCallback),
      "curl_easy_setopt( CURLOPT_WRITEFUNCTION )");
  handleCurlResult(curl_easy_setopt(_curl, CURLOPT_FILE, (void*)&rs),
                   "curl_easy_setopt( CURLOPT_FILE )");
  handleCurlResult(curl_easy_setopt(_curl, CURLOPT_NOPROGRESS, 0L),
                   "curl_easy_setopt( CURLOPT_NOPROGRESS )");
  handleCurlResult(curl_easy_setopt(_curl, CURLOPT_PROGRESSFUNCTION, xprogress),
                   "curl_easy_setopt( CURLOPT_PROGRESSFUNCTION )");
  handleCurlResult(curl_easy_setopt(_curl, CURLOPT_PROGRESSDATA, this),
                   "curl_easy_setopt( CURLOPT_PROGRESSDATA )");

  if (!proxyServerAddress.empty()) {
    handleCurlResult(
        curl_easy_setopt(_curl, CURLOPT_PROXY, proxyServerAddress.c_str()),
        "curl_easy_setup( CURLOPT_PROXY )");

    if (!proxyServerUserName.empty()) {
      handleCurlResult(
          curl_easy_setopt(_curl, CURLOPT_PROXYAUTH, CURLAUTH_BASIC),
          "curl_easy_setup( CURLOPT_PROXYAUTH )");

      handleCurlResult(curl_easy_setopt(_curl, CURLOPT_PROXYUSERNAME,
                                        proxyServerUserName.c_str()),
                       "curl_easy_setup( CURLOPT_PROXYUSERNAME )");
      handleCurlResult(curl_easy_setopt(_curl, CURLOPT_PROXYPASSWORD,
                                        proxyServerPassword.c_str()),
                       "curl_easy_setup( CURLOPT_PROXYPASSWORD )");
    }
  }
  //	handleCurlResult( curl_easy_setopt( _curl, CURLOPT_HEADER, "Accept:
  // text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"),
  //"curl_easy_setopt( CURLOPT_HEADER )" ); 	handleCurlResult(
  // curl_easy_setopt( _curl, CURLOPT_HEADER, "Cookie: c_enabled$=true;
  // clientPrefs=||||lightg; asi_segs=C10030|C10033|C10001|C10023|C0;
  //__atuvc=1%7C34; netseer_cm=done;
  // s_pers=%20bc%3D1%7C1408760936887%3B%20s_nr%3D1408675432563-New%7C1416451432563%3B;
  // s_sess=%20s_cc%3Dtrue%3B%20s_sq%3Dnasdaqprod%253D%252526pid%25253DNYSE%25252520Companies%25252520-%25252520NASDAQ.com%252526pidt%25253D1%252526oid%25253Dhttp%2525253A//www.nasdaq.com/screening/companies-by-industry.aspx%2525253Fexchange%2525253DNYSE%25252526render%2525253Ddownload%252526ot%25253DA%3B"),
  //"curl_easy_setopt( CURLOPT_HEADER )" );

  // set a timeout of 40 seconds - should be enough. What is the default
  // timeout?
  if (timeout > 0)
    handleCurlResult(curl_easy_setopt(_curl, CURLOPT_TIMEOUT, timeout),
                     "curl_easy_setopt( CURLOPT_TIMEOUT )");

  handleCurlResult(
      curl_easy_setopt(_curl, CURLOPT_USERAGENT, userAgent.c_str()),
      "curl_easy_setopt( CURLOPT_USERAGENT )");
  handleCurlResult(curl_easy_perform(_curl), "curl_easy_perform");

  long responseCode;

  curl_easy_getinfo(_curl, CURLINFO_RESPONSE_CODE, &responseCode);

  if (_canceled)
    throw InternetException(InternetException::REQUEST_CANCELLED, -1);

  if (responseCode != 200 && responseCode != 0)
    throw InternetException(InternetException::HTTP_RESPONSE_ERROR,
                            responseCode);

  return StringPtr(new std::string(rs));
}

CurlHTTPRequest::CurlHTTPRequest(const std::wstring& serverName, bool http,
                                 bool verifyPeer, bool verifyHost)
    : HTTPRequestBase(serverName, http),
      _curl(curl_easy_init()),
      _cancel(false),
      _canceled(false),
      m_verifyPeer(verifyPeer),
      m_verifyHost(verifyHost) {
  if (!_curl)
    throw InternetException(InternetException::CURL_ERROR, -1,
                            _T( "Curl initialization error" ));
}

CurlHTTPRequest::~CurlHTTPRequest() { curl_easy_cleanup(_curl); }

void CurlHTTPRequest::handleCurlResult(unsigned int res, const char* name) {
  std::wstring message;
  message << name << ", " << curl_easy_strerror((CURLcode)res) << " (" << res
          << ")";
  switch (res) {
    case CURLE_OK:
      return;
    case CURLE_UNSUPPORTED_PROTOCOL:  /* 1 */
    case CURLE_FAILED_INIT:           /* 2 */
    case CURLE_URL_MALFORMAT:         /* 3 */
    case CURLE_COULDNT_RESOLVE_PROXY: /* 5 */
    case CURLE_COULDNT_RESOLVE_HOST:  /* 6 */
      throw InternetException(InternetException::CURL_ERROR, (CURLcode)res,
                              message);
    case CURLE_COULDNT_CONNECT: /* 7 */
      throw InternetException(InternetException::FAILED_TO_CONNECT, res,
                              message);
    case CURLE_FTP_WEIRD_SERVER_REPLY: /* 8 */
    case CURLE_REMOTE_ACCESS_DENIED:   /* 9 a service was denied by the server
                                                                                                                                           due to lack of access - when login fails
                                                                                                                                           this is not returned. */
    case CURLE_OBSOLETE10:             /* 10 - NOT USED */
    case CURLE_FTP_WEIRD_PASS_REPLY:   /* 11 */
    case CURLE_OBSOLETE12:             /* 12 - NOT USED */
    case CURLE_FTP_WEIRD_PASV_REPLY:   /* 13 */
    case CURLE_FTP_WEIRD_227_FORMAT:   /* 14 */
    case CURLE_FTP_CANT_GET_HOST:      /* 15 */
    case CURLE_OBSOLETE16:             /* 16 - NOT USED */
    case CURLE_FTP_COULDNT_SET_TYPE:   /* 17 */
    case CURLE_PARTIAL_FILE:           /* 18 */
    case CURLE_FTP_COULDNT_RETR_FILE:  /* 19 */
    case CURLE_OBSOLETE20:             /* 20 - NOT USED */
    case CURLE_QUOTE_ERROR:            /* 21 - quote command failure */
    case CURLE_HTTP_RETURNED_ERROR:    /* 22 */
    case CURLE_WRITE_ERROR:            /* 23 */
    case CURLE_OBSOLETE24:             /* 24 - NOT USED */
    case CURLE_UPLOAD_FAILED:          /* 25 - failed upload "command" */
    case CURLE_READ_ERROR:             /* 26 - couldn't open/read from file */
    case CURLE_OUT_OF_MEMORY:          /* 27 */
    case CURLE_OPERATION_TIMEDOUT:     /* 28 - the timeout time was reached */
      throw InternetException(InternetException::ErrorType::OPERATION_TIMEDOUT,
                              res, message);
    case CURLE_OBSOLETE29:             /* 29 - NOT USED */
    case CURLE_FTP_PORT_FAILED:        /* 30 - FTP PORT operation failed */
    case CURLE_FTP_COULDNT_USE_REST:   /* 31 - the REST command failed */
    case CURLE_OBSOLETE32:             /* 32 - NOT USED */
    case CURLE_RANGE_ERROR:            /* 33 - RANGE "command" didn't work */
    case CURLE_HTTP_POST_ERROR:        /* 34 */
    case CURLE_SSL_CONNECT_ERROR:      /* 35 - wrong when connecting with SSL */
    case CURLE_BAD_DOWNLOAD_RESUME:    /* 36 - couldn't resume download */
    case CURLE_FILE_COULDNT_READ_FILE: /* 37 */
    case CURLE_LDAP_CANNOT_BIND:       /* 38 */
    case CURLE_LDAP_SEARCH_FAILED:     /* 39 */
    case CURLE_OBSOLETE40:             /* 40 - NOT USED */
    case CURLE_FUNCTION_NOT_FOUND:     /* 41 */
      throw InternetException(InternetException::CURL_ERROR, (CURLcode)res,
                              message);
    case CURLE_ABORTED_BY_CALLBACK: /* 42 */
      throw InternetException(InternetException::REQUEST_CANCELLED,
                              (CURLcode)res, message);
    case CURLE_BAD_FUNCTION_ARGUMENT: /* 43 */
    case CURLE_OBSOLETE44:            /* 44 - NOT USED */
    case CURLE_INTERFACE_FAILED:      /* 45 - CURLOPT_INTERFACE failed */
    case CURLE_OBSOLETE46:            /* 46 - NOT USED */
    case CURLE_TOO_MANY_REDIRECTS:    /* 47 - catch endless re-direct loops */
    case CURLE_UNKNOWN_TELNET_OPTION: /* 48 - User specified an unknown option
                                       */
    case CURLE_TELNET_OPTION_SYNTAX:  /* 49 - Malformed telnet option */
    case CURLE_OBSOLETE50:            /* 50 - NOT USED */
    case CURLE_PEER_FAILED_VERIFICATION: /* 51 - peer's certificate or
                                            fingerprint wasn't verified fine */
      throw InternetException(InternetException::CURL_ERROR, (CURLcode)res,
                              message);
    case CURLE_GOT_NOTHING: /* 52 - when this is a specific error */
      // will interpret this as an empty result string
      return;
    case CURLE_SSL_ENGINE_NOTFOUND:  /* 53 - SSL crypto engine not found */
    case CURLE_SSL_ENGINE_SETFAILED: /* 54 - can not set SSL crypto engine as
                                        default */
    case CURLE_SEND_ERROR:           /* 55 - failed sending network data */
    case CURLE_RECV_ERROR:      /* 56 - failure in receiving network data */
    case CURLE_OBSOLETE57:      /* 57 - NOT IN USE */
    case CURLE_SSL_CERTPROBLEM: /* 58 - problem with the local certificate */
    case CURLE_SSL_CIPHER:      /* 59 - couldn't use specified cipher */
    case CURLE_SSL_CACERT:      /* 60 - problem with the CA cert (path?) */
    case CURLE_BAD_CONTENT_ENCODING:  /* 61 - Unrecognized transfer encoding */
    case CURLE_LDAP_INVALID_URL:      /* 62 - Invalid LDAP URL */
    case CURLE_FILESIZE_EXCEEDED:     /* 63 - Maximum file size exceeded */
    case CURLE_USE_SSL_FAILED:        /* 64 - Requested FTP SSL level failed */
    case CURLE_SEND_FAIL_REWIND:      /* 65 - Sending the data requires a rewind
                                                                                                                                          that failed */
    case CURLE_SSL_ENGINE_INITFAILED: /* 66 - failed to initialise ENGINE */
    case CURLE_LOGIN_DENIED:          /* 67 - user: password or similar was not
                                                                                                                                          accepted and we failed to login */
    case CURLE_TFTP_NOTFOUND:         /* 68 - file not found on server */
    case CURLE_TFTP_PERM:             /* 69 - permission problem on server */
    case CURLE_REMOTE_DISK_FULL:      /* 70 - out of disk space on server */
    case CURLE_TFTP_ILLEGAL:          /* 71 - Illegal TFTP operation */
    case CURLE_TFTP_UNKNOWNID:        /* 72 - Unknown transfer ID */
    case CURLE_REMOTE_FILE_EXISTS:    /* 73 - File already exists */
    case CURLE_TFTP_NOSUCHUSER:       /* 74 - No such user */
    case CURLE_CONV_FAILED:           /* 75 - conversion failed */
    case CURLE_CONV_REQD:             /* 76 - caller must register conversion
                                                                                                                                                  callbacks using curl_easy_setopt options
                                                                                                                                                  CURLOPT_CONV_FROM_NETWORK_FUNCTION:
                                                                                                                                                  CURLOPT_CONV_TO_NETWORK_FUNCTION: and
                                                                                                                                                  CURLOPT_CONV_FROM_UTF8_FUNCTION */
    case CURLE_SSL_CACERT_BADFILE: /* 77 - could not load CACERT file: missing
                                                                                                                                       or wrong format */
    case CURLE_REMOTE_FILE_NOT_FOUND: /* 78 - remote file not found */
    case CURLE_SSH:                   /* 79 - error from the SSH layer: somewhat
                                                                                                                                          generic so the error message will be of
                                                                                                                                          interest when this has happened */

    case CURLE_SSL_SHUTDOWN_FAILED: /* 80 - Failed to shut down the SSL
                                                                                                                                        connection */
    case CURLE_AGAIN:               /* 81 - socket is not ready for send/recv:
                                                                                                                                        wait till it's ready and try again (Added
                                                                                                                                        in 7.18.2) */
    case CURLE_SSL_CRL_BADFILE:     /* 82 - could not load CRL file: missing or
                                                                                                                                        wrong format (Added in 7.19.0) */
    case CURLE_SSL_ISSUER_ERROR:    /* 83 - Issuer check failed.  (Added in
                                                                                                                                        7.19.0) */
    case CURL_LAST:                 /* never use! */
    default:
      throw InternetException(InternetException::CURL_ERROR, res, message);
  }
}
