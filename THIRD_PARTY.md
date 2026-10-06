# Third-party material

The library takes no dependencies beyond the platform's own APIs and
system libraries. What follows is data the repository ships and tools
that need outside packages; none of it is linked into the library.

## Data

| What | Where | License |
|---|---|---|
| SADIE II database, subject D1 (KU100), University of York, as the default HRTF | `data/hrtf/sadie2-ku100-48k.maudhrtf`, generated from it | Apache License 2.0, `data/hrtf/apache-2.0/LICENSE-2.0.txt`; notice in `data/hrtf/sadie2-ku100.notice.txt` |

## Tools

| Tool | Needs | License |
|---|---|---|
| `tools/sofa_to_maudhrtf.py` | numpy | BSD 3-Clause |
| `tools/sofa_to_maudhrtf.py` | h5py | BSD 3-Clause |
| `tools/near_field_table.py` | numpy | BSD 3-Clause |
