import React from 'react'

interface TextSpan {
  text: string
  bold?: boolean
  italic?: boolean
  underline?: boolean
  reverse?: boolean
  fgColor?: number
  bgColor?: number
}

// Parse IRC formatting codes into spans
function parseIRCFormatting(text: string): TextSpan[] {
  const spans: TextSpan[] = []
  let currentSpan: TextSpan = { text: '' }
  let bold = false
  let italic = false
  let underline = false
  let reverse = false
  let fgColor: number | undefined
  let bgColor: number | undefined

  let i = 0
  while (i < text.length) {
    const char = text.charCodeAt(i)

    switch (char) {
      case 0x02: // Bold
        if (currentSpan.text) {
          spans.push(currentSpan)
        }
        bold = !bold
        currentSpan = { text: '', bold, italic, underline, reverse, fgColor, bgColor }
        break

      case 0x1D: // Italic
        if (currentSpan.text) {
          spans.push(currentSpan)
        }
        italic = !italic
        currentSpan = { text: '', bold, italic, underline, reverse, fgColor, bgColor }
        break

      case 0x1F: // Underline
        if (currentSpan.text) {
          spans.push(currentSpan)
        }
        underline = !underline
        currentSpan = { text: '', bold, italic, underline, reverse, fgColor, bgColor }
        break

      case 0x16: // Reverse
        if (currentSpan.text) {
          spans.push(currentSpan)
        }
        reverse = !reverse
        currentSpan = { text: '', bold, italic, underline, reverse, fgColor, bgColor }
        break

      case 0x0F: // Reset
        if (currentSpan.text) {
          spans.push(currentSpan)
        }
        bold = italic = underline = reverse = false
        fgColor = bgColor = undefined
        currentSpan = { text: '' }
        break

      case 0x03: // Color
        if (currentSpan.text) {
          spans.push(currentSpan)
        }
        i++
        // Parse foreground color
        if (i < text.length && /[0-9]/.test(text[i])) {
          let colorStr = text[i]
          i++
          if (i < text.length && /[0-9]/.test(text[i])) {
            colorStr += text[i]
            i++
          }
          fgColor = parseInt(colorStr, 10) % 16

          // Check for background color
          if (i < text.length && text[i] === ',') {
            i++
            if (i < text.length && /[0-9]/.test(text[i])) {
              let bgColorStr = text[i]
              i++
              if (i < text.length && /[0-9]/.test(text[i])) {
                bgColorStr += text[i]
                i++
              }
              bgColor = parseInt(bgColorStr, 10) % 16
            }
          }
        } else {
          // No color digits, reset colors
          fgColor = bgColor = undefined
        }
        i-- // Compensate for loop increment
        currentSpan = { text: '', bold, italic, underline, reverse, fgColor, bgColor }
        break

      case 0x04: // Hex color (HexChat extension) - skip for now
        // Skip hex colors
        break

      default:
        // Regular character
        currentSpan.text += text[i]
    }
    i++
  }

  // Push remaining text
  if (currentSpan.text) {
    spans.push(currentSpan)
  }

  return spans
}

interface MessageRendererProps {
  text: string
}

function MessageRenderer({ text }: MessageRendererProps) {
  const spans = parseIRCFormatting(text)

  return (
    <>
      {spans.map((span, index) => {
        const style: React.CSSProperties = {}
        const classes: string[] = []

        if (span.bold) classes.push('irc-bold')
        if (span.italic) classes.push('irc-italic')
        if (span.underline) classes.push('irc-underline')
        if (span.reverse) classes.push('irc-reverse')

        if (span.fgColor !== undefined) {
          style.color = `var(--irc-color-${span.fgColor})`
        }
        if (span.bgColor !== undefined) {
          style.backgroundColor = `var(--irc-color-${span.bgColor})`
        }

        return (
          <span key={index} className={classes.join(' ')} style={style}>
            {span.text}
          </span>
        )
      })}
    </>
  )
}

export default MessageRenderer
