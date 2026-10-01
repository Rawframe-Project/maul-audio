# Changelog

All notable changes to this project are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
the project uses [Semantic Versioning](https://semver.org/). Before
1.0.0, any minor release may change the API, the ABI and every data
format.

## [Unreleased]

### Added

- The library skeleton: the build, the family rules and tools, the
  version and result API (`maudGetVersion`, `maudResultName`) and the
  library profile.
- Result codes for the coming device and spatial APIs, and the
  allocator owner objects take (`maudAllocator`).
- Channel layouts (`maudChannelLayout`): mono, stereo, quad, 5.1, 7.1
  and 7.1.4 in the Windows speaker mask order, with each channel's
  speaker and its nominal Recommendation ITU-R BS.2051 position.
- Exact conversion between interleaved frames and one array per
  channel (`maudInterleave`, `maudDeinterleave`), real-time safe.
- A benchmark of the conversions.
