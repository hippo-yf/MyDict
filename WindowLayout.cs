using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;

namespace MyDict
{
    public class WindowLayout
    {
        public double Left { get; set; }
        public double Top { get; set; }
        public double Width { get; set; }
        public double Height { get; set; }
        public bool Maximized { get; set; }
        public bool IsValid() { return Finite(Left) && Finite(Top) && Finite(Width) && Finite(Height) && Width > 0 && Height > 0; }
        private static bool Finite(double value) { return !double.IsNaN(value) && !double.IsInfinity(value) && Math.Abs(value) < 1000000; }
    }
    internal static class WindowPlacement
    {
        [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
        [StructLayout(LayoutKind.Sequential)] private struct MonitorInfo { public int Size; public Rect Monitor, Work; public uint Flags; }
        [DllImport("user32.dll")] private static extern IntPtr MonitorFromRect(ref Rect rect, uint flags);
        [DllImport("user32.dll")] private static extern bool GetMonitorInfo(IntPtr monitor, ref MonitorInfo info);
        public static void Restore(Window window, WindowLayout saved)
        {
            if (saved == null || !saved.IsValid()) return;
            var source = PresentationSource.FromVisual(window);
            var toDevice = source?.CompositionTarget?.TransformToDevice ?? System.Windows.Media.Matrix.Identity;
            var fromDevice = source?.CompositionTarget?.TransformFromDevice ?? System.Windows.Media.Matrix.Identity;
            var a = toDevice.Transform(new Point(saved.Left, saved.Top));
            var b = toDevice.Transform(new Point(saved.Left + saved.Width, saved.Top + saved.Height));
            var rect = new Rect { Left = (int)a.X, Top = (int)a.Y, Right = (int)b.X, Bottom = (int)b.Y };
            var monitor = MonitorFromRect(ref rect, 2); // nearest monitor when the original screen is disconnected
            var info = new MonitorInfo { Size = Marshal.SizeOf(typeof(MonitorInfo)) };
            var work = SystemParameters.WorkArea;
            if (GetMonitorInfo(monitor, ref info))
            {
                var leftTop = fromDevice.Transform(new Point(info.Work.Left, info.Work.Top));
                var rightBottom = fromDevice.Transform(new Point(info.Work.Right, info.Work.Bottom));
                work = new System.Windows.Rect(leftTop, rightBottom);
            }
            window.WindowStartupLocation = WindowStartupLocation.Manual;
            window.Width = Math.Max(window.MinWidth, Math.Min(saved.Width, work.Width));
            window.Height = Math.Max(window.MinHeight, Math.Min(saved.Height, work.Height));
            window.Left = Math.Max(work.Left, Math.Min(saved.Left, work.Right - window.Width));
            window.Top = Math.Max(work.Top, Math.Min(saved.Top, work.Bottom - Math.Min(window.Height, work.Height)));
            if (saved.Maximized) window.WindowState = WindowState.Maximized;
        }
    }
}
