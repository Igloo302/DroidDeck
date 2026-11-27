package com.droiddeck.launcher.files

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Typeface
import android.text.Editable
import android.text.InputType
import android.text.Spanned
import android.text.TextWatcher
import android.text.style.TabStopSpan
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.inputmethod.BaseInputConnection
import android.widget.EditText
import android.widget.LinearLayout
import androidx.compose.ui.text.TextRange
import kotlin.math.ceil
import kotlin.math.roundToInt

internal class TextEditorView(context: Context) : LinearLayout(context) {
    internal class Field(context: Context) : EditText(context) {
        var selectionChanged: (() -> Unit)? = null
        var scrolled: (() -> Unit)? = null

        override fun onSelectionChanged(start: Int, end: Int) {
            super.onSelectionChanged(start, end)
            selectionChanged?.invoke()
        }

        override fun onScrollChanged(x: Int, y: Int, oldX: Int, oldY: Int) {
            super.onScrollChanged(x, y, oldX, oldY)
            scrolled?.invoke()
        }
    }

    private class EditorTabStop(position: Int) : TabStopSpan.Standard(position)
    internal data class LineNumber(val number: Int, val baseline: Float)

    val field = Field(context)
    private val numberPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.MONOSPACE
        textAlign = Paint.Align.RIGHT
    }
    private var lineStarts = intArrayOf(0)
    private var mutedColor = 0
    private var cursorColor = 0
    private var synchronizing = false
    private var changingText = false
    private var readOnly = false
    private var previousLineHeight = Float.NaN
    private var onChanged: (String, TextRange) -> Unit = { _, _ -> }
    private val gutter = object : View(context) {
        override fun onDraw(canvas: Canvas) {
            super.onDraw(canvas)
            val current = lineAt(field.selectionEnd.coerceAtLeast(0)) + 1
            for (line in visibleLineNumbers()) {
                numberPaint.color = if (line.number == current) cursorColor else mutedColor
                canvas.drawText(line.number.toString(), width - numberPaint.measureText(" "), line.baseline, numberPaint)
            }
        }
    }
    private val editableKeys: android.text.method.KeyListener

    init {
        orientation = HORIZONTAL
        field.apply {
            background = null
            setPadding(0, 0, 0, 0)
            gravity = Gravity.TOP or Gravity.START
            typeface = Typeface.MONOSPACE
            includeFontPadding = false
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            setHorizontallyScrolling(true)
            setTextIsSelectable(true)
            isVerticalScrollBarEnabled = true
            isHorizontalScrollBarEnabled = true
        }
        editableKeys = requireNotNull(field.keyListener)
        addView(gutter, LayoutParams(0, LayoutParams.MATCH_PARENT))
        addView(field, LayoutParams(0, LayoutParams.MATCH_PARENT, 1f))
        field.selectionChanged = {
            gutter.invalidate()
            if (!changingText) publish()
        }
        field.scrolled = { gutter.invalidate() }
        field.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(text: CharSequence?, start: Int, count: Int, after: Int) { changingText = true }
            override fun onTextChanged(text: CharSequence?, start: Int, before: Int, count: Int) = Unit
            override fun afterTextChanged(text: Editable) {
                updateDocument(text)
                changingText = false
                publish()
            }
        })
    }

    fun bind(
        text: String,
        selection: TextRange,
        readOnly: Boolean,
        textSizePx: Float,
        lineHeightPx: Float,
        textColor: Int,
        mutedColor: Int,
        cursorColor: Int,
        onChanged: (String, TextRange) -> Unit,
    ) {
        this.onChanged = onChanged
        this.mutedColor = mutedColor
        this.cursorColor = cursorColor
        synchronizing = true
        try {
            val metricsChanged = field.textSize != textSizePx || numberPaint.textSize != textSizePx || previousLineHeight != lineHeightPx
            if (metricsChanged) {
                field.setTextSize(TypedValue.COMPLEX_UNIT_PX, textSizePx)
                field.setLineSpacing(lineHeightPx - field.paint.fontSpacing, 1f)
                numberPaint.textSize = textSizePx
                previousLineHeight = lineHeightPx
            }
            field.setTextColor(textColor)
            field.highlightColor = (cursorColor and 0x00ffffff) or 0x55000000
            if (this.readOnly != readOnly) {
                this.readOnly = readOnly
                field.keyListener = if (readOnly) null else editableKeys
                if (!readOnly) field.setRawInputType(InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS)
            }
            val textChanged = field.text.toString() != text
            if (textChanged) field.setText(text)
            else if (metricsChanged) updateDocument(field.text)
            val start = selection.start.coerceIn(0, text.length)
            val end = selection.end.coerceIn(0, text.length)
            val selectionChanged = field.selectionStart != start || field.selectionEnd != end
            if (selectionChanged) {
                BaseInputConnection.removeComposingSpans(field.text)
                field.setSelection(start, end)
            }
            if (textChanged || selectionChanged) field.post {
                field.bringPointIntoView(field.selectionEnd.coerceAtLeast(0))
                gutter.invalidate()
            }
        } finally {
            synchronizing = false
        }
        gutter.invalidate()
    }

    private fun publish() {
        if (!synchronizing && field.selectionStart >= 0 && field.selectionEnd >= 0)
            onChanged(field.text.toString(), TextRange(field.selectionStart, field.selectionEnd))
    }

    private fun updateDocument(text: Editable) {
        for (span in text.getSpans(0, text.length, EditorTabStop::class.java)) text.removeSpan(span)
        val starts = ArrayList<Int>().apply { add(0) }
        val stops = ArrayList<Int>()
        val space = field.paint.measureText(" ")
        var column = 0
        var paragraphStart = 0
        var runStart = 0
        var advance = 0f
        fun applyStops(end: Int) {
            for (stop in stops) text.setSpan(EditorTabStop(stop), paragraphStart, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            stops.clear()
        }
        for (i in text.indices) when (text[i]) {
            '\n' -> {
                applyStops(i + 1)
                starts.add(i + 1)
                paragraphStart = i + 1; runStart = i + 1; column = 0; advance = 0f
            }
            '\t' -> {
                val padding = EDITOR_TAB_WIDTH - column % EDITOR_TAB_WIDTH
                advance += field.paint.measureText(text, runStart, i)
                // Native tab stops are integer pixels; accumulate their rounding within this paragraph.
                val stop = ceil(advance + padding * space).toInt()
                stops.add(stop)
                advance = stop.toFloat(); runStart = i + 1; column += padding
            }
            else -> column++
        }
        applyStops(text.length)
        lineStarts = starts.toIntArray()
        val width = numberPaint.measureText("${lineStarts.size}  ").roundToInt()
        if (gutter.layoutParams.width != width) gutter.layoutParams = gutter.layoutParams.apply { this.width = width }
        gutter.invalidate()
    }

    private fun lineAt(offset: Int): Int {
        val index = lineStarts.binarySearch(offset)
        return if (index >= 0) index else (-index - 2).coerceAtLeast(0)
    }

    internal fun visibleLineNumbers(): List<LineNumber> {
        val layout = field.layout ?: return emptyList()
        val first = layout.getLineForVertical(field.scrollY)
        val last = layout.getLineForVertical(field.scrollY + field.height)
        return (first..last).mapNotNull { visualLine ->
            val offset = layout.getLineStart(visualLine)
            val line = lineAt(offset)
            if (lineStarts[line] != offset) null else LineNumber(line + 1,
                (layout.getLineBaseline(visualLine) + field.totalPaddingTop - field.scrollY).toFloat())
        }
    }
}
