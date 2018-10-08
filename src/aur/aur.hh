#ifndef AUR_HH
#define AUR_HH

#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <curl/curl.h>
#include <systemd/sd-event.h>

#include "request.hh"
#include "response.hh"

namespace aur {

class Aur {
 public:
  using RpcResponseCallback = std::function<int(StatusOr<RpcResponse>)>;
  using RawResponseCallback = std::function<int(StatusOr<RawResponse>)>;
  using CloneResponseCallback = std::function<int(StatusOr<CloneResponse>)>;

  // Construct a new Aur object, rooted at the given URL, e.g.
  // https://aur.archlinux.org.
  explicit Aur(const std::string& baseurl);
  ~Aur();

  Aur(const Aur&) = delete;
  Aur& operator=(const Aur&) = delete;
  Aur(Aur&&) = default;

  // Sets the maximum number of allowed simultaneous connections open to the AUR
  // server at any given time. Set to 0 to specify unlimited connections.
  void SetMaxConnections(long connections);

  // Sets the connection timeout, in seconds for each connection to the AUR
  // server. Set to 0 to specify no timeout.
  void SetConnectTimeout(long timeout);

  // Asynchronously issue an RPC request. The callback will be invoked when the
  // call completes.
  void QueueRpcRequest(const RpcRequest& request,
                       const RpcResponseCallback& callback);

  // Asynchronously issue an RPC request. The callback will be invoked when the
  // call completes.
  void QueueRawRpcRequest(const RpcRequest& request,
                          const RawResponseCallback& callback);

  // Asynchronously issue a download request. The callback will be invoked when
  // the call completes.
  void QueueTarballRequest(const RawRequest& request,
                           const RawResponseCallback& callback);

  // Asynchronous issue a PKGBUILD request. The callbcak will be invoked when
  // the call complete.
  void QueuePkgbuildRequest(const RawRequest& request,
                            const RawResponseCallback& callback);

  // Clone a git repository.
  void QueueCloneRequest(const CloneRequest& request,
                         const CloneResponseCallback& callback);

  // Wait for all pending requests to complete. Returns non-zero if any request
  // failed or was cancelled by a callback.
  int Wait();

 private:
  template <typename RequestType>
  void QueueRequest(
      const Request& request,
      const typename RequestType::ResponseHandlerType::CallbackType& callback);

  void StartRequest(CURL* curl);
  int FinishRequest(CURL* curl, CURLcode result, bool dispatch_callback);

  int ProcessDoneEvents();
  void Cancel();

  class ActiveRequests {
   public:
    ActiveRequests() {}

    void Add(CURL* curl) { curls_.insert(curl); }
    void Add(sd_event_source* event_source) {
      event_sources_.insert(event_source);
    }

    void Remove(CURL* curl) { curls_.erase(curl); }
    void Remove(sd_event_source* event_source) {
      event_sources_.erase(event_source);
    }

    bool IsEmpty() const { return curls_.empty() && event_sources_.empty(); }

   private:
    std::unordered_set<CURL*> curls_;
    std::unordered_set<sd_event_source*> event_sources_;
  };

  enum DebugLevel {
    // No debugging.
    DEBUG_NONE,

    // Enable Curl's verbose output to stderr
    DEBUG_VERBOSE_STDERR,

    // Enable Curl debug handler, write outbound requests made to a file
    DEBUG_REQUESTS,
  };

  static int SocketCallback(CURLM* curl, curl_socket_t s, int action,
                            void* userdata, void* socketp);
  static int TimerCallback(CURLM* curl, long timeout_ms, void* userdata);
  static int OnIO(sd_event_source* s, int fd, uint32_t revents, void* userdata);
  static int OnTimer(sd_event_source* s, uint64_t usec, void* userdata);
  static int OnCloneExit(sd_event_source* s, const siginfo_t* si,
                         void* userdata);

  const std::string baseurl_;

  long connect_timeout_ = 10;

  CURLM* curl_;
  ActiveRequests active_requests_;
  std::unordered_map<int, sd_event_source*> active_io_;
  std::unordered_map<int, int> translate_fds_;

  sigset_t saved_ss_;
  sd_event* event_ = nullptr;
  sd_event_source* timer_ = nullptr;

  DebugLevel debug_level_ = DEBUG_NONE;
  std::ofstream debug_stream_;
};

}  // namespace aur

#endif  // AUR_HH
