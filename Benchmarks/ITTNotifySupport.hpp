#ifdef WITH_ITT_NOTIFY
#include <ittnotify.h>
#include <sstream>
#endif // WITH_ITT_NOTIFY

class VTuneAPIInterface {
#ifdef WITH_ITT_NOTIFY
  ___itt_domain* domain;
#endif // WITH_ITT_NOTIFY

public:
  VTuneAPIInterface(char const*)
#ifdef WITH_ITT_NOTIFY
      : domain(__itt_domain_create("wcoj"))
#endif // WITH_ITT_NOTIFY
            {};
  template <typename... DescriptorTypes> void startSampling(DescriptorTypes... tasknameComponents) {
#ifdef WITH_ITT_NOTIFY
    std::stringstream taskname;
    (taskname << ... << tasknameComponents);
    auto task = __itt_string_handle_create(taskname.str().c_str());
    __itt_resume();
    __itt_task_begin(domain, __itt_null, __itt_null, task);
#endif // WITH_ITT_NOTIFY
  }
  void stopSampling() {
#ifdef WITH_ITT_NOTIFY
    __itt_task_end(domain);
    __itt_pause();
#endif // WITH_ITT_NOTIFY
  }
};
