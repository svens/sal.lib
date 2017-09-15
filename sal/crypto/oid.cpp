#include <sal/crypto/oid.hpp>


__sal_begin


namespace crypto { namespace oid {


const oid_t
  collective_state_or_province_name = "2.5.4.8.1",
  collective_street_address = "2.5.4.9.1",
  common_name = "2.5.4.3",
  country_name = "2.5.4.6",
  description = "2.5.4.13",
  given_name = "2.5.4.42",
  locality_name = "2.5.4.7",
  organization_name = "2.5.4.10",
  organizational_unit_name = "2.5.4.11",
  serial_number = "2.5.4.5",
  state_or_province_name = "2.5.4.8",
  street_address = "2.5.4.9",
  surname = "2.5.4.4",
  title = "2.5.4.12";


} // namespace oid


const std::string &alias_or_oid (const oid_t &oid) noexcept
{
  static const std::pair<oid_t, std::string> oid_alias[] =
  {
    { oid::common_name, "CN" },
    { oid::country_name, "C" },
    { oid::locality_name, "L" },
    { oid::organization_name, "O" },
    { oid::organizational_unit_name, "OU" },
    { oid::state_or_province_name, "ST" },
    { oid::street_address, "STREET" },
  };

  for (const auto &alias: oid_alias)
  {
    if (oid == alias.first)
    {
      return alias.second;
    }
  }

  return oid;
}


} // namespace crypto


__sal_end