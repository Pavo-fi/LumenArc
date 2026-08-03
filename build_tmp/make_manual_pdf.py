# MANUAL.md -> PDF 生成器（fpdf2，内嵌中文字体）
# 用法: python make_manual_pdf.py <manual_md> <out_pdf>
import re
import sys

from fpdf import FPDF

FONT_BODY = "C:/Windows/Fonts/simsun.ttc"   # 宋体正文
FONT_BOLD = "C:/Windows/Fonts/simhei.ttf"   # 黑体粗体/标题


def sanitize(s: str) -> str:
    # 字体中可能缺失的符号降级
    return (s.replace("\u25b6", ">")      # ▶
             .replace("\u2022", "-")      # •
             .replace("`", ""))


def md_inline(s: str) -> str:
    """保留 fpdf markdown 支持的 **粗体**；去掉链接语法，保留文字。"""
    s = sanitize(s)
    s = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", s)
    return s


class ManualPdf(FPDF):
    def footer(self):
        self.set_y(-12)
        self.set_font("cjk", "", 8)
        self.set_text_color(140, 140, 140)
        self.cell(0, 8, f"追光者 Lumen Arc v1.1 操作手册  ·  第 {self.page_no()} 页",
                  align="C")


def render(md_path: str, out_path: str):
    lines = open(md_path, encoding="utf-8").read().splitlines()

    pdf = ManualPdf("P", "mm", "A4")
    pdf.set_auto_page_break(True, 18)
    pdf.set_margins(18, 16, 18)
    pdf.add_font("cjk", "", FONT_BODY)
    pdf.add_font("cjk", "B", FONT_BOLD)
    pdf.add_page()
    usable = pdf.w - pdf.l_margin - pdf.r_margin

    i = 0
    title_done = 0

    def para(text, size=10.5, style="", lh=6.0, color=(30, 30, 30), indent=0):
        pdf.set_font("cjk", style, size)
        pdf.set_text_color(*color)
        pdf.set_x(pdf.l_margin + indent)
        pdf.multi_cell(usable - indent, lh, md_inline(text), markdown=True)
        pdf.ln(0.6)

    while i < len(lines):
        line = lines[i].rstrip()

        # ---------- 表格块 ----------
        if line.lstrip().startswith("|"):
            block = []
            while i < len(lines) and lines[i].lstrip().startswith("|"):
                block.append(lines[i].strip())
                i += 1
            rows = []
            for r, raw in enumerate(block):
                cells = [sanitize(re.sub(r"\*\*([^*]+)\*\*", r"\1", c.strip()))
                         for c in raw.strip("|").split("|")]
                if r == 1 and all(set(c) <= set("-: ") for c in cells):
                    continue  # 分隔行
                rows.append(cells)
            if not rows:
                continue
            ncols = max(len(r) for r in rows)
            for r in rows:
                r += [""] * (ncols - len(r))
            pdf.set_font("cjk", "", 9.5)
            try:
                with pdf.table(text_align="LEFT", line_height=5.6,
                               first_row_as_headings=True,
                               headings_style=__import__("fpdf").fonts.FontFace(
                                   emphasis="BOLD", fill_color=(240, 230, 210)),
                               borders_layout="ALL",
                               cell_fill_color=(250, 250, 250),
                               cell_fill_mode="ROWS") as table:
                    for row in rows:
                        tr = table.row()
                        for cell in row:
                            tr.cell(cell)
            except Exception as e:  # 表格渲染失败退化为文本
                print(f"[warn] table fallback: {e}")
                for row in rows:
                    para("  ".join(c for c in row if c), size=9.5)
            pdf.ln(2)
            continue

        # ---------- 标题 ----------
        if line.startswith("# "):
            if title_done == 0:
                pdf.ln(30)
                pdf.set_font("cjk", "B", 24)
                pdf.set_text_color(40, 40, 40)
                pdf.multi_cell(usable, 12, sanitize(line[2:]), align="C")
                title_done = 1
            else:
                pdf.ln(4)
                para(line[2:], size=18, style="B")
            i += 1
            continue
        if title_done == 1 and line.strip() and not line.startswith("#"):
            # 主标题下一行 = 副标题
            pdf.set_font("cjk", "B", 15)
            pdf.set_text_color(180, 140, 40)
            pdf.multi_cell(usable, 9, sanitize(line.strip()), align="C")
            pdf.ln(8)
            title_done = 2
            i += 1
            continue
        if line.startswith("## "):
            pdf.ln(3)
            para(line[3:], size=14.5, style="B", color=(150, 110, 20))
            pdf.set_draw_color(200, 170, 90)
            pdf.set_line_width(0.5)
            y = pdf.get_y()
            pdf.line(pdf.l_margin, y, pdf.l_margin + usable, y)
            pdf.ln(2.5)
            i += 1
            continue
        if line.startswith("### "):
            pdf.ln(2)
            para(line[4:], size=12, style="B")
            i += 1
            continue

        # ---------- 分隔线 ----------
        if line.strip() == "---":
            pdf.ln(2)
            pdf.set_draw_color(210, 210, 210)
            pdf.set_line_width(0.2)
            y = pdf.get_y()
            pdf.line(pdf.l_margin, y, pdf.l_margin + usable, y)
            pdf.ln(3)
            i += 1
            continue

        # ---------- 引用块 ----------
        if line.lstrip().startswith(">"):
            pdf.set_fill_color(245, 242, 235)
            pdf.set_font("cjk", "", 9.8)
            pdf.set_text_color(90, 80, 50)
            pdf.set_x(pdf.l_margin + 3)
            pdf.multi_cell(usable - 3, 5.6, md_inline(line.lstrip()[1:].strip()),
                           markdown=True, fill=True)
            pdf.ln(1)
            i += 1
            continue

        # ---------- 列表 ----------
        m = re.match(r"^(\s*)[-*]\s+(.*)$", line)
        if m:
            indent = 4 + len(m.group(1)) * 2
            para("· " + m.group(2), indent=indent)
            i += 1
            continue
        m = re.match(r"^(\s*)(\d+)\.\s+(.*)$", line)
        if m:
            para(f"{m.group(2)}. {m.group(3)}", indent=4)
            i += 1
            continue

        # ---------- 空行 / 普通段落 ----------
        if not line.strip():
            pdf.ln(1.2)
        else:
            para(line)
        i += 1

    pdf.output(out_path)
    print(f"[ok] {out_path}  pages={pdf.page_no()}")


if __name__ == "__main__":
    render(sys.argv[1], sys.argv[2])
