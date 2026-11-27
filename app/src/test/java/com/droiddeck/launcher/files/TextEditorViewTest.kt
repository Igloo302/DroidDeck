package com.droiddeck.launcher.files

import android.app.Activity
import android.content.ClipboardManager
import android.content.Context
import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.os.Looper
import android.view.View
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import androidx.compose.ui.text.TextRange
import java.io.File
import org.junit.After
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class TextEditorViewTest {
    private val activity = Robolectric.buildActivity(Activity::class.java).setup()

    @After fun close() { activity.pause().stop().destroy() }

    private inner class Fixture(densityDpi: Int = 160, fontScale: Float = 1f) {
        val context = activity.get().createConfigurationContext(Configuration(activity.get().resources.configuration).apply {
            this.densityDpi = densityDpi
            this.fontScale = fontScale
        })
        val view = TextEditorView(context)
        private val scale = densityDpi / 160f * fontScale
        var model = ""
        var selection = TextRange.Zero
        var callbacks = 0
        var echo = false
        var locked = false

        init { activity.get().setContentView(view) }

        fun bind(text: String = model, range: TextRange = selection, readOnly: Boolean = locked) {
            model = text; selection = range; locked = readOnly
            view.bind(text, range, readOnly, EDITOR_FONT_SIZE_SP * scale, EDITOR_LINE_HEIGHT_SP * scale,
                Color.BLACK, Color.GRAY, Color.BLUE) { value, selected ->
                model = value; selection = selected; callbacks++
                if (echo) bind()
            }
        }

        fun layout(width: Int = 480, height: Int = 800) {
            shadowOf(Looper.getMainLooper()).idle()
            view.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY))
            view.layout(0, 0, width, height)
        }

        fun draw(): Bitmap = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888).also {
            val canvas = Canvas(it)
            canvas.drawColor(Color.WHITE)
            view.draw(canvas)
        }
    }

    @Test fun literalTabsDisplayAtFourColumnStopsAndSurviveEditing() {
        val f = Fixture()
        f.bind("target:\n\tcommand\nname\tvalue\tend\n")
        f.layout()
        val editable = f.view.field.text
        val tab = editable.indexOf('\t')
        val space = f.view.field.paint.measureText(" ")
        assertEquals(4f * space, f.view.field.layout.getPrimaryHorizontal(tab + 1), 1f)
        f.view.field.text.insert(tab, "ab")
        f.layout()
        assertEquals(4f * space, f.view.field.layout.getPrimaryHorizontal(tab + 3), 1f)
        f.view.field.text.insert(tab, "cd")
        f.layout()
        assertEquals(8f * space, f.view.field.layout.getPrimaryHorizontal(tab + 5), 1f)
        assertEquals("target:\ncdab\tcommand\nname\tvalue\tend\n", f.model)
        assertArrayEquals(f.model.toByteArray(), f.view.field.text.toString().toByteArray())
        assertSame(editable, f.view.field.text)
        assertTrue(f.callbacks > 0)
    }

    @Test fun twentyThousandLinesRemainEditableAndNavigateAtHighDensityAndFontScale() {
        val f = Fixture(480, 1.6f)
        val text = "x\n".repeat(20_000) + "last\tvalue"
        assertTrue(text.toByteArray().size < EDITOR_MAX_EDIT_BYTES)
        f.bind(text)
        f.layout(1080, 1920)
        assertEquals(1920, f.view.height)
        assertEquals(1920, f.view.field.height)
        assertTrue(f.view.field.layout.height > 262_142)
        val match = findEditorMatches(text, "last").single()
        f.bind(range = TextRange(match.first, match.last + 1))
        f.layout(1080, 1920)
        assertTrue(f.view.field.scrollY > 262_142)
        assertEquals("last", f.view.field.text.substring(f.view.field.selectionStart, f.view.field.selectionEnd))
        assertTrue(f.view.visibleLineNumbers().any { it.number == 20_001 && it.baseline in 0f..1920f })
        assertTrue(f.view.visibleLineNumbers().size < 40)
        f.view.field.text.replace(match.first, match.last + 1, "edited")
        assertEquals("x\n".repeat(20_000) + "edited\tvalue", f.model)
        f.draw().recycle()
    }

    @Test fun fractionalTabStopsDoNotCollapseAtExactColumnsOrLeakAcrossParagraphs() {
        val text = "a\tX\nabcd\tY\nabcdefgh\tZ\na\tabcd\tend\n\t\tend\n"
        for (fontScale in listOf(1.07f, 1.13f, 1.37f, 1.6f)) {
            val f = Fixture(240, fontScale)
            f.bind(text)
            f.layout()
            val space = f.view.field.paint.measureText(" ")
            var column = 0
            for (i in text.indices) when (text[i]) {
                '\n' -> column = 0
                '\t' -> {
                    val padding = EDITOR_TAB_WIDTH - column % EDITOR_TAB_WIDTH
                    val actual = f.view.field.layout.getPrimaryHorizontal(i + 1) - f.view.field.layout.getPrimaryHorizontal(i)
                    assertEquals("tab at $i with font scale $fontScale", padding * space, actual, 1.1f)
                    column += padding
                }
                else -> column++
            }
            assertEquals(text, f.view.field.text.toString())
        }
    }

    @Test fun veryLongSingleLineUsesNativeHorizontalScrolling() {
        val f = Fixture(320)
        val text = "x".repeat(40_000) + "\tend"
        f.bind(text)
        f.layout()
        f.bind(range = TextRange(text.length))
        f.layout()
        assertEquals(480, f.view.width)
        assertEquals(800, f.view.field.height)
        assertTrue(f.view.field.scrollX > 262_142)
        assertEquals(text, f.view.field.text.toString())
        f.view.field.text.append("!")
        assertEquals(text + "!", f.model)
    }

    @Test fun readOnlyKeepsSelectionCopyAndScrollingAndCanBecomeEditableAgain() {
        val f = Fixture()
        f.bind("first\tvalue\n".repeat(1000) + "copy me", readOnly = true)
        f.layout()
        f.bind(range = TextRange(f.model.length - 7, f.model.length))
        f.layout()
        assertNull(f.view.field.keyListener)
        assertTrue(f.view.field.isEnabled)
        assertTrue(f.view.field.scrollY > 0)
        assertTrue(f.view.field.onTextContextMenuItem(android.R.id.copy))
        val clipboard = f.context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        assertEquals("copy me", clipboard.primaryClip!!.getItemAt(0).text.toString())
        f.bind(readOnly = false)
        assertNotNull(f.view.field.keyListener)
    }

    @Test fun equalStateEchoPreservesEditableAndImeCompositionWhileSearchClearsIt() {
        val f = Fixture()
        f.bind("start ", TextRange(6))
        f.layout()
        f.view.field.requestFocus()
        val editable = f.view.field.text
        val connection = requireNotNull(f.view.field.onCreateInputConnection(EditorInfo()))
        f.echo = true
        assertTrue(connection.setComposingText("candidate", 1))
        assertSame(editable, f.view.field.text)
        assertEquals("start candidate", f.model)
        assertEquals(6, BaseInputConnection.getComposingSpanStart(editable))
        assertEquals(15, BaseInputConnection.getComposingSpanEnd(editable))
        val calls = f.callbacks
        f.bind()
        assertEquals(calls, f.callbacks)
        assertEquals(6, BaseInputConnection.getComposingSpanStart(editable))
        f.bind(range = TextRange(0, 5))
        assertEquals(-1, BaseInputConnection.getComposingSpanStart(editable))
        assertEquals(TextRange(0, 5), f.selection)
    }

    @Test fun smallFileGutterAndTrailingLineFollowNativeLayout() {
        val f = Fixture()
        f.bind("target:\n\tcommand\nname\tvalue\n")
        f.layout(360, 640)
        assertEquals(listOf(1, 2, 3, 4), f.view.visibleLineNumbers().map { it.number })
        assertEquals(0, f.view.field.scrollY)
        val png = File("build/preview-renders/text-editor/portrait.png").apply { parentFile.mkdirs() }
        f.draw().also { bitmap ->
            png.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
            bitmap.recycle()
        }
    }
}
