# FreeRenderFPS host matrix summary

**FAKE RESULTS: this run used a generated results file, not an AviUtl2 export. It only proves the validator and report path.**

Run directory: `D:\AviUtl Free Render FPS\experiments\host_matrix\run_dryrun`
Results file: `D:\AviUtl Free Render FPS\experiments\host_matrix\run_dryrun\FAKE_auto_export_jobs.tsv.results.tsv`

Overall: **INCOMPLETE**

| Case | Project -> target | Expected frames | Export | host_loaded_project | output_file | frame_count | stream_rate | av_duration | no_pairing | near_duplicates | Overall |
|---|---|---:|---|---|---|---|---|---|---|---|---|
| FreeFPS OFF 66 -> 66 | 66 -> 66 | 330 | OK | PASS | PASS | PASS | PASS | PASS | N/A | PASS | **PASS** |
| 30 -> 60 | 30 -> 60 | 300 | OK | PASS | PASS | PASS | PASS | PASS | PASS | PASS | **PASS** |
| 66 -> 60 | 66 -> 60 | 300 | OK | PASS | PASS | PASS | PASS | PASS | N/A | PASS | **PASS** |
| 66 -> 60000/1001 | 66 -> 60000/1001 | 300 | OK | PASS | PASS | PASS | PASS | PASS | N/A | PASS | **PASS** |
| 66 -> 24 | 66 -> 24 | 120 | OK | PASS | PASS | PASS | PASS | PASS | N/A | PASS | **PASS** |
| 66 -> 120 | 66 -> 120 | 600 | OK | PASS | PASS | PASS | PASS | PASS | N/A | PASS | **PASS** |
| 120 -> 60 | 120 -> 60 | 300 | OK | PASS | PASS | PASS | PASS | PASS | N/A | PASS | **PASS** |
| 30 -> 120 | 30 -> 120 | 600 | NOT_RUN | - | - | - | - | - | - | - | **NOT_RUN** |
| 66 -> 60000/1001, 60 s | 66 -> 60000/1001 | 3597 | OK | PASS | PASS | PASS | PASS | PASS | N/A | WARN | **PASS** |

## Details

### FreeFPS OFF 66 -> 66 (`off_66_66`)
- host_loaded_project: PASS - rate=66/1 frame_max=329 expected project 66 fps, frame_max=329
- output_file: PASS - 24705 bytes
- frame_count: PASS - decoded 330, expected 330, ffprobe -count_frames 330
- stream_rate: PASS - r_frame_rate 66/1 vs target 66/1
- av_duration: PASS - video 5.000000 s, audio 5.000000 s, \|delta\| 0.000000 s, tolerance < 0.015152 s
- no_pairing: N/A - target/project ratio 1/1 is not an integer >= 2
- near_duplicates: PASS - 0 boundaries with MAD < 0.013053819444444446; 0 at the final boundary ignored (end clamp); 0 counted, limit 1

### 30 -> 60 (`30_60`)
- host_loaded_project: PASS - rate=30/1 frame_max=149 expected project 30 fps, frame_max=149
- output_file: PASS - 22830 bytes
- frame_count: PASS - decoded 300, expected 300, ffprobe -count_frames 300
- stream_rate: PASS - r_frame_rate 60/1 vs target 60/1
- av_duration: PASS - video 5.000000 s, audio 5.000000 s, \|delta\| 0.000000 s, tolerance < 0.016667 s
- no_pairing: PASS - r=2 in-group median MAD 0.1426 vs out-of-group 0.1436, ratio 0.9932, need >= 0.25
- near_duplicates: PASS - 1 boundaries with MAD < 0.014302083333333335; 1 at the final boundary ignored (end clamp); 0 counted, limit 1

### 66 -> 60 (`66_60`)
- host_loaded_project: PASS - rate=66/1 frame_max=329 expected project 66 fps, frame_max=329
- output_file: PASS - 22024 bytes
- frame_count: PASS - decoded 300, expected 300, ffprobe -count_frames 300
- stream_rate: PASS - r_frame_rate 60/1 vs target 60/1
- av_duration: PASS - video 5.000000 s, audio 5.000000 s, \|delta\| 0.000000 s, tolerance < 0.016667 s
- no_pairing: N/A - target/project ratio 10/11 is not an integer >= 2
- near_duplicates: PASS - 0 boundaries with MAD < 0.014193576388888891; 0 at the final boundary ignored (end clamp); 0 counted, limit 1

### 66 -> 60000/1001 (`66_5994`)
- host_loaded_project: PASS - rate=66/1 frame_max=329 expected project 66 fps, frame_max=329
- output_file: PASS - 22768 bytes
- frame_count: PASS - decoded 300, expected 300, ffprobe -count_frames 300
- stream_rate: PASS - r_frame_rate 60000/1001 vs target 60000/1001
- av_duration: PASS - video 5.005000 s, audio 5.000000 s, \|delta\| 0.005000 s, tolerance < 0.016683 s
- no_pairing: N/A - target/project ratio 10000/11011 is not an integer >= 2
- near_duplicates: PASS - 0 boundaries with MAD < 0.01420355902777778; 0 at the final boundary ignored (end clamp); 0 counted, limit 1

### 66 -> 24 (`66_24`)
- host_loaded_project: PASS - rate=66/1 frame_max=329 expected project 66 fps, frame_max=329
- output_file: PASS - 12964 bytes
- frame_count: PASS - decoded 120, expected 120, ffprobe -count_frames 120
- stream_rate: PASS - r_frame_rate 24/1 vs target 24/1
- av_duration: PASS - video 5.000000 s, audio 5.000000 s, \|delta\| 0.000000 s, tolerance < 0.041667 s
- no_pairing: N/A - target/project ratio 4/11 is not an integer >= 2
- near_duplicates: PASS - 0 boundaries with MAD < 0.034724826388888894; 0 at the final boundary ignored (end clamp); 0 counted, limit 1

### 66 -> 120 (`66_120`)
- host_loaded_project: PASS - rate=66/1 frame_max=329 expected project 66 fps, frame_max=329
- output_file: PASS - 37781 bytes
- frame_count: PASS - decoded 600, expected 600, ffprobe -count_frames 600
- stream_rate: PASS - r_frame_rate 120/1 vs target 120/1
- av_duration: PASS - video 5.000000 s, audio 5.000000 s, \|delta\| 0.000000 s, tolerance < 0.008333 s
- no_pairing: N/A - target/project ratio 20/11 is not an integer >= 2
- near_duplicates: PASS - 0 boundaries with MAD < 0.007322048611111111; 0 at the final boundary ignored (end clamp); 0 counted, limit 1

### 120 -> 60 (`120_60`)
- host_loaded_project: PASS - rate=120/1 frame_max=599 expected project 120 fps, frame_max=599
- output_file: PASS - 22125 bytes
- frame_count: PASS - decoded 300, expected 300, ffprobe -count_frames 300
- stream_rate: PASS - r_frame_rate 60/1 vs target 60/1
- av_duration: PASS - video 5.000000 s, audio 5.000000 s, \|delta\| 0.000000 s, tolerance < 0.016667 s
- no_pairing: N/A - target/project ratio 1/2 is not an integer >= 2
- near_duplicates: PASS - 0 boundaries with MAD < 0.014253906250000002; 0 at the final boundary ignored (end clamp); 0 counted, limit 1

### 30 -> 120 (`30_120`)
- export: no row in results file
- no checks run

### 66 -> 60000/1001, 60 s (`66_5994_60s`)
- host_loaded_project: PASS - rate=66/1 frame_max=3959 expected project 66 fps, frame_max=3959
- output_file: PASS - 214990 bytes
- frame_count: PASS - decoded 3597, expected 3597, ffprobe -count_frames 3597
- stream_rate: PASS - r_frame_rate 60000/1001 vs target 60000/1001
- av_duration: PASS - video 60.008333 s, audio 60.000000 s, \|delta\| 0.008333 s, tolerance < 0.016683 s
- no_pairing: N/A - target/project ratio 10000/11011 is not an integer >= 2
- near_duplicates: WARN - 11 boundaries with MAD < 0.0012465277777777778; 0 at the final boundary ignored (end clamp); 11 counted, limit 1

