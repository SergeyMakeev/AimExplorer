using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using OxyPlot;
using OxyPlot.Axes;
using OxyPlot.Series;
using System.Diagnostics;
using System.Windows.Threading;
using System.Windows.Media;
using System.ComponentModel;
using System.IO;

namespace Visualizer
{
    class PlotViewModel : INotifyPropertyChanged
    {
        class FrameData
        {
            public Int32 rawIndex;
            public Int64 eventTs;
            public Int64 captureTs;
            public Int64 freqTs;
            public double tsMs;
        }

        byte[] buffer = new byte[4];

        Int64 timeStampStart = 0;

        List<FrameData> frames = new List<FrameData>();

        List<GraphItem> lines = new List<GraphItem>();

        LineSeries gamepadX = new LineSeries();
        LineSeries gamepadY = new LineSeries();

        LinearAxis xAxis;
        LinearAxis yAxis = new LinearAxis();

        public readonly PlotModel model;

        public PlotViewModel()
        {
            model = new PlotModel
            {
                Title = "",
                Subtitle = ""
            };

            xAxis = new LinearAxis();
            xAxis.Minimum = 0;
            xAxis.Maximum = 10000;
            xAxis.MaximumPadding = 1;
            xAxis.MinimumPadding = 1;
            xAxis.Position = AxisPosition.Bottom;
            xAxis.Title = "Time [ms]";
            xAxis.MajorGridlineStyle = LineStyle.Solid;
            xAxis.MinorGridlineStyle = LineStyle.None;
            model.Axes.Add(xAxis);

            yAxis.Maximum = 180;
            yAxis.Minimum = -180;
            yAxis.Title = "Angle [deg]";
            yAxis.MaximumPadding = 1;
            yAxis.MinimumPadding = 1;
            yAxis.MajorGridlineStyle = LineStyle.Solid;
            yAxis.MinorGridlineStyle = LineStyle.None;
            model.Axes.Add(yAxis);

            gamepadX.MarkerType = MarkerType.None;
            gamepadX.StrokeThickness = 2;
            gamepadX.MarkerSize = 3;
            gamepadX.Title = "Gamepad X";

            gamepadY.MarkerType = MarkerType.None;
            gamepadY.StrokeThickness = 2;
            gamepadY.MarkerSize = 3;
            gamepadY.Title = "Gamepad Y";

            gamepadX.Points.Capacity = 8192;
            gamepadY.Points.Capacity = 8192;
            frames.Capacity = 8192;

            this.RaisePropertyChanged("model");
        }

        public void SetTitle(string title)
        {
            model.Title = title;
        }

        public void SetSubTitle(string subtitle)
        {
            model.Subtitle = subtitle;
        }

        public void Update(NativeMethods.CapturePacket packet)
        {
            if (timeStampStart == 0)
            {
                timeStampStart = packet.eventTimeStamp;
            }

            Int64 dtFromStart = (packet.eventTimeStamp - timeStampStart);
            //Int64 timestampMs = (dtFromStart * (Int64)1000L) / packet.freq;
            double dblTimestampMs = (double)dtFromStart / ((double)packet.freq / 1000.0);


            if (lines.Count >= 1)
            {
                lines[0].value = packet.val01;
                lines[0].oxyLine.Points.Add(new DataPoint(dblTimestampMs, lines[0].DegValue));
            }

            if (lines.Count >= 2)
            {
                lines[1].value = packet.val02;
                lines[1].oxyLine.Points.Add(new DataPoint(dblTimestampMs, lines[1].DegValue));
            }

            gamepadX.Points.Add(new DataPoint(dblTimestampMs, packet.gamepadX * 100.0f));
            gamepadY.Points.Add(new DataPoint(dblTimestampMs, packet.gamepadY * 100.0f));


            frames.Add(new FrameData() { rawIndex = packet.frameIndex, eventTs = packet.eventTimeStamp, captureTs = packet.captureTimeStamp, freqTs = packet.freq, tsMs = dblTimestampMs } );
        }

        public void Invalidate()
        {
            model.InvalidatePlot(true);
        }

        public void Clear()
        {
            lines.Clear();
            frames.Clear();

            model.Series.Clear();

            gamepadX.Points.Clear();
            gamepadY.Points.Clear();

            model.Series.Add(gamepadX);
            model.Series.Add(gamepadY);
            timeStampStart = 0;
        }

        public void ClearData()
        {

            frames.Clear();

            gamepadX.Points.Clear();
            gamepadY.Points.Clear();

            foreach(GraphItem line in lines)
            {
                line.oxyLine.Points.Clear();
            }

            Invalidate();
            timeStampStart = 0;
        }



        public void AddLine(GraphItem line)
        {
            lines.Add(line);

            LineSeries oxyLine = new LineSeries();
            oxyLine.MarkerType = MarkerType.None;
            oxyLine.StrokeThickness = 2;
            oxyLine.MarkerSize = 3;
            oxyLine.Title = line.Name;
            oxyLine.Points.Capacity = 8192;
            line.oxyLine = oxyLine;

            model.Series.Add(oxyLine);
        }

        public void ExportToExcel(string name)
        {
            string header = "#;frame;event ts;capture ts;err ts;freq;ms;dt ms;gamepad_x %;gamepad_y %;";
            foreach(GraphItem item in lines)
            {
                header += item.oxyLine.Title;
                header += ";";
                header += "delta " + item.oxyLine.Title;
                header += ";";
                header += item.oxyLine.Title + " / sec";
                header += ";";
            }

            for (int i = 0; i < lines.Count; i++)
            {
                GraphItem item = lines[i];
                System.Diagnostics.Debug.Assert(item.oxyLine.Points.Count == frames.Count, "Data inconsistency!");
            }

            System.Diagnostics.Debug.Assert(gamepadX.Points.Count == frames.Count, "Data inconsistency!");
            System.Diagnostics.Debug.Assert(gamepadY.Points.Count == frames.Count, "Data inconsistency!");

            string row = "";

            using (StreamWriter writer = File.CreateText(name))
            {
                writer.WriteLine(header);

                for(int frame = 0; frame < frames.Count; frame++)
                {
                    row = frame.ToString();
                    row += ";";
                    row += frames[frame].rawIndex.ToString();
                    row += ";";
                    row += frames[frame].eventTs.ToString();
                    row += ";";
                    row += frames[frame].captureTs.ToString();
                    row += ";";
                    row += (frames[frame].captureTs - frames[frame].eventTs).ToString();
                    row += ";";
                    row += frames[frame].freqTs.ToString();
                    row += ";";
                    row += frames[frame].tsMs.ToString();
                    row += ";";

                    double dt = 0.0;
                    if (frame > 0)
                    {
                        dt = frames[frame].tsMs - frames[frame - 1].tsMs;
                    }
                    row += dt.ToString();
                    row += ";";

                    row += gamepadX.Points[frame].Y.ToString();
                    row += ";";
                    row += gamepadY.Points[frame].Y.ToString();
                    row += ";";

                    for (int i = 0; i < lines.Count; i++)
                    {
                        GraphItem item = lines[i];
                        row += item.oxyLine.Points[frame].Y.ToString();
                        row += ";";

                        double dv = 0.0;
                        if (frame > 0)
                        {
                            dv = NormalizeAngle(item.oxyLine.Points[frame].Y - item.oxyLine.Points[frame - 1].Y);
                        }

                        double perSec = 0.0;
                        if (dt > 0.0000000001)
                        {
                            perSec = 1000.0 / dt;
                        }

                        row += dv.ToString();
                        row += ";";
                        row += (dv * perSec).ToString();
                        row += ";";
                    }
                    writer.WriteLine(row);
                }
                writer.Close();
            }
        }

        static double NormalizeAngle(double v)
        {
            if (v > 180.0)
                return (v - 360.0);

            if (v < -180.0)
                return (v + 360.0);

            return v;
        }

        public event PropertyChangedEventHandler PropertyChanged;

        protected void RaisePropertyChanged(string property)
        {
            var handler = this.PropertyChanged;
            if (handler != null)
            {
                handler(this, new PropertyChangedEventArgs(property));
            }
        }

    }
}
