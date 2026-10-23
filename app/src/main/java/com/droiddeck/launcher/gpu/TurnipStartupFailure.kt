package com.droiddeck.launcher.gpu

import java.io.File
import java.io.RandomAccessFile

object TurnipStartupFailure {
    const val CODE = "GPU_UNSUPPORTED"
    private const val TAIL_BYTES = 256 * 1024
    private val unsupported = Regex(
        "device \\(chip_id = (?:0x)?([0-9a-fA-F]+), gpu_id = [0-9]+\\) is unsupported \\(VK_ERROR_INCOMPATIBLE_DRIVER\\)",
    )

    fun unsupportedChipId(log: File?): String? {
        if (log == null || !log.isFile) return null
        return runCatching {
            RandomAccessFile(log, "r").use { file ->
                val length = file.length()
                val count = minOf(length, TAIL_BYTES.toLong()).toInt()
                file.seek(length - count)
                val bytes = ByteArray(count)
                file.readFully(bytes)
                unsupported.find(bytes.toString(Charsets.ISO_8859_1))
                    ?.groupValues?.get(1)?.lowercase()?.let { "0x$it" }
            }
        }.getOrNull()
    }
}
