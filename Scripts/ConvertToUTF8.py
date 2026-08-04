#!/usr/bin/env python3

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path


UTF8_BOM = b"\xef\xbb\xbf"
UTF16_LE_BOM = b"\xff\xfe"
UTF16_BE_BOM = b"\xfe\xff"
UTF32_LE_BOM = b"\xff\xfe\x00\x00"
UTF32_BE_BOM = b"\x00\x00\xfe\xff"

""" 
사용법
# 파일 하나 변환
python convert_encoding.py MyFile.cpp

# 현재 디렉터리의 파일 검사
python convert_encoding.py .

# 하위 디렉터리를 포함해 C/C++ 관련 파일 검사
python convert_encoding.py . -r \
  -e cpp -e h -e c -e hpp -e inl

# 먼저 변환 대상만 확인
python convert_encoding.py . -r \
  -e cpp -e h --dry-run

# 원본을 .bak로 백업하면서 변환
python convert_encoding.py . -r \
  -e cpp -e h --backup
""" 


def has_korean_text(text: str) -> bool:
    """CP949 오판을 줄이기 위해 한글 또는 CP949 특유 문자가 있는지 확인한다."""
    return any(
        "\uac00" <= char <= "\ud7a3"       # 한글 완성형
        or "\u3131" <= char <= "\u318e"    # 한글 호환 자모
        or "\u1100" <= char <= "\u11ff"    # 한글 자모
        for char in text
    )


def detect_encoding(data: bytes) -> str:
    """
    파일의 인코딩을 보수적으로 추정한다.

    반환값:
        utf-8-sig, utf-16-le, utf-16-be, utf-32-le, utf-32-be,
        utf-8, cp949, unknown
    """
    # UTF-32 BOM은 UTF-16 BOM과 앞부분이 겹치므로 먼저 검사한다.
    if data.startswith(UTF32_LE_BOM):
        return "utf-32-le"
    if data.startswith(UTF32_BE_BOM):
        return "utf-32-be"
    if data.startswith(UTF8_BOM):
        return "utf-8-sig"
    if data.startswith(UTF16_LE_BOM):
        return "utf-16-le"
    if data.startswith(UTF16_BE_BOM):
        return "utf-16-be"

    try:
        data.decode("utf-8", errors="strict")
        return "utf-8"
    except UnicodeDecodeError:
        pass

    try:
        decoded = data.decode("cp949", errors="strict")
    except UnicodeDecodeError:
        return "unknown"

    # ASCII만 있는 파일이나 우연히 CP949로 디코딩되는 바이너리 파일을
    # CP949로 판단하지 않도록 최소한의 텍스트 휴리스틱을 적용한다.
    if has_korean_text(decoded):
        return "cp949"

    return "unknown"


def convert_file(
    path: Path,
    *,
    backup: bool = False,
    dry_run: bool = False,
) -> bool:
    """CP949 파일이면 BOM 없는 UTF-8로 변환한다."""
    try:
        data = path.read_bytes()
    except OSError as error:
        print(f"[오류] 읽기 실패: {path} ({error})", file=sys.stderr)
        return False

    encoding = detect_encoding(data)

    if encoding != "cp949":
        print(f"[건너뜀] {path} ({encoding})")
        return False

    try:
        text = data.decode("cp949", errors="strict")
        converted = text.encode("utf-8")
    except UnicodeError as error:
        print(f"[오류] 변환 실패: {path} ({error})", file=sys.stderr)
        return False

    if dry_run:
        print(f"[변환 예정] {path}: CP949 → UTF-8 (BOM 없음)")
        return True

    try:
        if backup:
            backup_path = path.with_name(path.name + ".bak")
            shutil.copy2(path, backup_path)

        # 임시 파일에 먼저 저장한 뒤 교체하여 변환 중 파일 손상을 줄인다.
        temporary_path = path.with_name(path.name + ".encoding_tmp")
        temporary_path.write_bytes(converted)
        temporary_path.replace(path)
    except OSError as error:
        print(f"[오류] 저장 실패: {path} ({error})", file=sys.stderr)
        return False

    print(f"[완료] {path}: CP949 → UTF-8 (BOM 없음)")
    return True


def collect_files(
    target: Path,
    *,
    recursive: bool,
    extensions: set[str] | None,
):
    if target.is_file():
        yield target
        return

    iterator = target.rglob("*") if recursive else target.glob("*")

    for path in iterator:
        if not path.is_file():
            continue

        if extensions is not None and path.suffix.lower() not in extensions:
            continue

        yield path


def parse_extensions(values: list[str] | None) -> set[str] | None:
    if not values:
        return None

    result: set[str] = set()

    for value in values:
        extension = value.lower()
        if not extension.startswith("."):
            extension = "." + extension
        result.add(extension)

    return result


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "파일 인코딩을 확인하고, CP949로 판단된 파일만 "
            "BOM 없는 UTF-8로 변환합니다."
        )
    )
    parser.add_argument(
        "target",
        type=Path,
        help="변환할 파일 또는 디렉터리",
    )
    parser.add_argument(
        "-r",
        "--recursive",
        action="store_true",
        help="하위 디렉터리까지 재귀적으로 검사",
    )
    parser.add_argument(
        "-e",
        "--extension",
        action="append",
        help="검사할 확장자. 여러 번 지정 가능. 예: -e cpp -e h",
    )
    parser.add_argument(
        "--backup",
        action="store_true",
        help="변환 전 원본을 .bak 파일로 백업",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="실제 파일을 변경하지 않고 변환 대상만 출력",
    )

    args = parser.parse_args()
    target: Path = args.target

    if not target.exists():
        print(f"[오류] 경로가 존재하지 않습니다: {target}", file=sys.stderr)
        return 1

    extensions = parse_extensions(args.extension)

    checked_count = 0
    converted_count = 0

    for path in collect_files(
        target,
        recursive=args.recursive,
        extensions=extensions,
    ):
        checked_count += 1

        if convert_file(
            path,
            backup=args.backup,
            dry_run=args.dry_run,
        ):
            converted_count += 1

    print()
    print(f"검사 파일: {checked_count}개")
    print(
        f"{'변환 예정' if args.dry_run else '변환 완료'}: "
        f"{converted_count}개"
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())