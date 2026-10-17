# AviUtl2 SDK headers used by experiments/aux2_output_probe

Copied byte-for-byte from `experiments/auto_export/sdk/` (no edits). That folder's
`SOURCE.md` describes the download: unofficial mirror
`https://github.com/aviutl2/aviutl2_sdk_mirror`, branch `main`, commit
`83cb19fe4fb328f56e3137d36e4ee7a1a85986aa`.

| File       | Bytes | SHA-256 |
|------------|------:|---------|
| plugin2.h  | 73769 | 92beb726111436aba0b82f606ed28585fd34a9c86af07a2cfb6cade56356d2ce |
| output2.h  |  5942 | f554762d01c62d91474dd858dbbc26fe4e2e5fe8ed3c85b64383f4a77294eeee |
| logger2.h  |  1560 | e6bdbbb54b1a9f9b4d4b33110dc602b499165268ad4b1e13048636e4a591829e |

`output2.h` is included here because `OUTPUT_PLUGIN_TABLE` and `OUTPUT_INFO` are
defined only there; `plugin2.h` forward-declares `OUTPUT_PLUGIN_TABLE` (line 36).
`config2.h` is not needed by this probe and is not copied.
