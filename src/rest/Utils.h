#include <llhttp.h>
#include "Request.h"

namespace asiodemo {
namespace utils {


rest::Request::Type llhttpToRequestType(llhttp_t* p);
void tolowerInPlace(std::string& str);


/// @brief removes leading and trailing whitespace
std::string trim(std::string const& sourceStr,
                 std::string const& trimStr = " \t\n\r");

}}