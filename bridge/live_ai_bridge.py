#!/usr/bin/env python3
"""ESP32 ABS_JSON -> 24-frame temporal buffer -> Unified AI.

The ESP32 currently reports every 2 s while V3 was trained at 5 s/timestep.
This runtime resamples onto a 5 s target cadence using timestamp_ms.

Rule outputs (fault_label, operating_mode, guardrail_status, etc.) are
reference-only and NEVER enter the trained 33-feature AI window.

AI energy output is PRE-GUARDRAIL ONLY. Firmware injection comes next,
after this live inference path is verified.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    print(r"PySerial missing. Install with .\.venv\Scripts\python.exe -m pip install pyserial")
    raise SystemExit(1)

ROOT=Path(__file__).resolve().parents[1]
ML=ROOT/"ml"
sys.path.insert(0,str(ML))

from unified_ai_inference import UnifiedAIEngine  # noqa:E402

PREFIX="ABS_JSON|"
AI_PREFIX="ABS_AI_RESULT|"
SCHEMA="abs.v1"
DEFAULT_URL="rfc2217://localhost:4001"


class Sampler:
    def __init__(self, interval_ms:int):
        self.interval=int(interval_ms)
        self.next_target=None
        self.previous=None

    def accept(self, timestamp_ms:int)->bool:
        t=int(timestamp_ms)
        if self.previous is not None and t < self.previous:
            self.next_target=None
        self.previous=t

        if self.next_target is None:
            self.next_target=t+self.interval
            return True

        if t < self.next_target:
            return False

        while self.next_target <= t:
            self.next_target += self.interval
        return True


def parse_line(line:str)->dict:
    if not line.startswith(PREFIX):
        raise ValueError("missing ABS_JSON prefix")
    obj=json.loads(line[len(PREFIX):])
    if obj.get("schema") != SCHEMA:
        raise ValueError(f"unsupported schema {obj.get('schema')!r}")
    return obj


def check_features(obj:dict, features:list[str])->None:
    missing=[f for f in features if f not in obj]
    if missing:
        raise KeyError("missing AI features: "+", ".join(missing))


def summary(result:dict)->str:
    d=result["fault_domain"]
    a=result["anomaly"]
    e=result["energy_recommendation"]
    parts=[f"domain={d['label']} ({d['confidence']:.3f})",
           f"anomaly={'YES' if a['flagged'] else 'NO'} score={a['score']:.3f}"]
    local=result["root_cause"]["local"]
    upstream=result["root_cause"]["upstream"]
    if local:
        parts.append(f"local={local['label']} ({local['confidence']:.3f})")
    if upstream:
        parts.append(f"upstream={upstream['label']} ({upstream['confidence']:.3f})")
    parts.append("AI-mode="+e["recommended_mode"])
    return " | ".join(parts)


def save_latest(result:dict)->Path:
    d=ROOT/"bridge"/"runtime"
    d.mkdir(parents=True,exist_ok=True)
    target=d/"latest_ai_result.json"
    tmp=d/"latest_ai_result.json.tmp"
    tmp.write_text(json.dumps(result,indent=2),encoding="utf-8")
    tmp.replace(target)
    return target


def smoke(engine:UnifiedAIEngine)->int:
    line=(ROOT/"bridge"/"captured_esp32_packet.txt").read_text(encoding="utf-8").strip()
    obj=parse_line(line)
    check_features(obj,engine.feature_columns)

    window=engine.new_buffer()
    for _ in range(engine.timesteps):
        window.add(obj)

    t0=time.perf_counter()
    result=engine.diagnose(window.as_array())
    ms=(time.perf_counter()-t0)*1000.0

    print()
    print("[ SMOKE TEST - FUNCTIONAL ONLY ]")
    print(f"Captured packet      : PASS")
    print(f"AI features          : {len(engine.feature_columns)}/{len(engine.feature_columns)}")
    print(f"Temporal buffer      : {window.count}/{engine.timesteps} READY")
    print(f"Inference            : PASS ({ms:.1f} ms)")
    print("NOTE                 : repeated frame test is not a performance test")
    print("Result               : "+summary(result))
    return 0


def live(engine:UnifiedAIEngine,url:str,sample_ms:int)->int:
    window=engine.new_buffer()
    sampler=Sampler(sample_ms)

    print()
    print("="*66)
    print(" AUTONOMOUS BASE STATION - LIVE TEMPORAL AI")
    print("="*66)
    print(f"Endpoint             : {url}")
    print(f"Temporal cadence     : {sample_ms/1000.0:.1f} s")
    print(f"Window               : {engine.timesteps} frames / {engine.timesteps*engine.sample_interval_seconds:.0f} s")
    print("Rule outputs          : REFERENCE ONLY - NOT AI FEATURES")
    print("Energy output         : PRE-GUARDRAIL ONLY")
    print("Press Ctrl+C to stop.")
    print()

    port=serial.serial_for_url(url,baudrate=115200,timeout=0.50)
    time.sleep(0.3)
    port.reset_input_buffer()
    print("[OK] ESP32 connected.")

    source=sampled=inferences=rejected=0
    try:
        while True:
            raw=port.readline()
            if not raw:
                continue
            line=raw.decode("utf-8",errors="replace").strip()
            if not line.startswith(PREFIX):
                continue
            source+=1

            try:
                obj=parse_line(line)
                check_features(obj,engine.feature_columns)
                stamp=int(obj["timestamp_ms"])
            except (ValueError,KeyError,TypeError,json.JSONDecodeError) as exc:
                rejected+=1
                print(f"[DROP] {exc}")
                continue

            if not sampler.accept(stamp):
                continue

            sampled+=1
            window.add(obj)

            ref_fault=obj.get("fault_label","UNKNOWN")
            ref_mode=obj.get("operating_mode","UNKNOWN")
            print(f"[SAMPLE {sampled:04d}] t={stamp} ms | window={window.count}/{engine.timesteps} | source-ref fault={ref_fault} mode={ref_mode}")

            if not window.ready:
                print(f"  AI WARMUP: {window.count}/{engine.timesteps}")
                continue

            t0=time.perf_counter()
            result=engine.diagnose(window.as_array())
            ms=(time.perf_counter()-t0)*1000.0
            inferences+=1

            enriched=dict(result)
            enriched["runtime"]={
                "source_schema":SCHEMA,
                "source_timestamp_ms":stamp,
                "accepted_temporal_samples":sampled,
                "inference_ms":ms,
                "source_reference_only":{
                    "fault_label":ref_fault,
                    "operating_mode":ref_mode,
                    "guardrail_status":obj.get("guardrail_status","UNKNOWN"),
                },
                "source_reference_used_as_ai_features":False,
            }

            target=save_latest(enriched)
            print(f"  [AI #{inferences}] {summary(result)} | {ms:.1f} ms")
            print(AI_PREFIX+json.dumps(enriched,separators=(",",":")))
            print(f"  Latest result: {target}")

    except KeyboardInterrupt:
        print("\nLive AI stopped by user.")
    finally:
        port.close()

    print()
    print("[ LIVE AI STATISTICS ]")
    print(f"ESP32 packets read : {source}")
    print(f"Temporal samples   : {sampled}")
    print(f"AI inferences      : {inferences}")
    print(f"Rejected packets   : {rejected}")
    return 0


def main()->int:
    p=argparse.ArgumentParser()
    p.add_argument("--url",default=DEFAULT_URL)
    p.add_argument("--sample-ms",type=int,default=5000)
    p.add_argument("--smoke-test",action="store_true")
    args=p.parse_args()

    engine=UnifiedAIEngine(ML)
    if args.smoke_test:
        return smoke(engine)
    return live(engine,args.url,args.sample_ms)


if __name__=="__main__":
    raise SystemExit(main())