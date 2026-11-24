#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
"""验证 Node 下载在超时或损坏响应后重试，并保留最终错误。"""
import hashlib
import importlib.util
import io
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('node_prepare', Path(__file__).resolve().parents[1] / 'node/prepare-node.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class DownloadTests(unittest.TestCase):
    @patch.object(prepare.time, 'sleep')
    @patch.object(prepare.urllib.request, 'urlopen')
    def test_timeout_then_success(self, request, sleep):
        request.side_effect = [TimeoutError('fixture timeout'), io.BytesIO(b'valid')]
        self.assertEqual(prepare.download('https://example.invalid/node'), b'valid')
        self.assertEqual(request.call_count, 2)
        sleep.assert_called_once_with(1)

    @patch.object(prepare.time, 'sleep')
    @patch.object(prepare.urllib.request, 'urlopen')
    def test_checksum_failure_then_success(self, request, sleep):
        request.side_effect = [io.BytesIO(b'truncated'), io.BytesIO(b'valid')]
        digest = hashlib.sha256(b'valid').hexdigest()
        self.assertEqual(prepare.download('https://example.invalid/node', digest), b'valid')
        self.assertEqual(request.call_count, 2)

    @patch.object(prepare.time, 'sleep')
    @patch.object(prepare.urllib.request, 'urlopen')
    def test_retries_are_bounded(self, request, sleep):
        request.side_effect = TimeoutError('fixture timeout')
        with self.assertRaises(TimeoutError):
            prepare.download('https://example.invalid/node')
        self.assertEqual(request.call_count, 4)
        self.assertEqual(sleep.call_count, 3)


if __name__ == '__main__':
    unittest.main()
