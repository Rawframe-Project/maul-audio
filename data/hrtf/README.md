# HRTF data

The default head-related transfer function set, in the format
`docs/hrtf-format.md` specifies, loaded at run time.

## `sadie2-ku100-48k.maudhrtf`

The SADIE II database's subject D1, a Neumann KU100 dummy head,
measured at 48 kHz by the University of York, under the Apache License
2.0 (`apache-2.0/LICENSE-2.0.txt`). `sadie2-ku100.notice.txt` carries the
copyright, the license notice, the reference the authors ask for and
the changes made; the same text is inside the file.

It was generated, not edited, from the published SOFA file:

- **Source:** `D1_HRIR_SOFA.zip` from https://zenodo.org/records/12092466
  (SADIE II v2-2, 2024), SHA-256
  `366321fa78f211bacc0ec6bea96701625b196a5db54ec16748b0eab0b9705f75`,
  member `D1_HRIR_SOFA/D1_48K_24bit_256tap_FIR_SOFA.sofa`, SHA-256
  `9af7cb19531e52fb7ae8ec92621e6ab62b1d5fe584b3742be36699a0ddb0ccd4`.
- **Command:** with numpy 2.5.3 and h5py 3.16.0,

  ```sh
  tools/sofa_to_maudhrtf.py D1_48K_24bit_256tap_FIR_SOFA.sofa \
      data/hrtf/sadie2-ku100-48k.maudhrtf \
      --name "SADIE II D1 (KU100), 48 kHz" \
      --license-file data/hrtf/sadie2-ku100.notice.txt
  ```

- **Result:** 1,652 directions on 37 rings, 128 taps at 48 kHz,
  measured at 1.2 m, format version 2, 853,896 bytes, SHA-256
  `6867385a5f475ff735151daeba145d12e5c7524c9a52cb1be37f65c587deac33`.
