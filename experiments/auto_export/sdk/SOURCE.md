# AviUtl2 SDK headers used by experiments/auto_export

Downloaded with curl on 2026-10-17 (UTC) from the unofficial mirror:

- Repository: https://github.com/aviutl2/aviutl2_sdk_mirror
- Branch `main`, commit `83cb19fe4fb328f56e3137d36e4ee7a1a85986aa`
  (committed 2026-10-10T03:04:59Z, from `GET /repos/aviutl2/aviutl2_sdk_mirror/commits/main`)
- Files (`include/aviutl2_sdk/`), UTF-8 encoded:

| File        | Bytes | SHA-256 |
|-------------|------:|---------|
| plugin2.h   | 73769 | 92beb726111436aba0b82f606ed28585fd34a9c86af07a2cfb6cade56356d2ce |
| output2.h   |  5942 | f554762d01c62d91474dd858dbbc26fe4e2e5fe8ed3c85b64383f4a77294eeee |
| logger2.h   |  1560 | e6bdbbb54b1a9f9b4d4b33110dc602b499165268ad4b1e13048636e4a591829e |
| config2.h   |  3475 | 03c80fe01628a04cf9c2b170e8cfc0315c5b83d951739e53417d844b497dbe68 |

`plugin2.h` includes no other SDK header. `output2.h`, `logger2.h` and
`config2.h` were downloaded as the brief asked; the plugin uses only `plugin2.h`
and `logger2.h`.

## Difference from `reference/aviutl2_sdk/`

The repository's `reference/aviutl2_sdk/` copy is Shift-JIS encoded and older
than this mirror revision. The mirror adds the following, which the reference
copy does not have:

- `EVENT_TYPE::CHANGE_EDIT_STATE` (mirror `plugin2.h` line 126), which the
  plugin needs for export start and end notifications;
- `EDIT_INFO::COLOR background`, `OBJECT_FLAG_TYPE`, and the
  `get_object_flag` / `set_object_flag` / `get_object_id` members.

The shared members used by this experiment (`get_edit_state`,
`get_edit_info`, `open_project_file`, `output_file`,
`register_event_listener`, `register_project_load_handler`,
`register_edit_menu_param`) are declared in both copies with the same signatures.

Files are not modified. Re-download and update this file if the mirror moves.
