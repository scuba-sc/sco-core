// SPDX-License-Identifier: GPL-3.0-only
using System;
using System.Collections.Generic;
using System.Text;

namespace Sco.Sdk
{
    /// <summary>Collects a preorder sco.widgets 1.0 tree with explicit stable node IDs.
    /// Build freezes the builder; pass Dispatch alongside the resulting nodes to Widgets.SetPanel.
    /// Construction and dispatch are game-thread only.</summary>
    public sealed class PanelBuilder
    {
        private static readonly UTF8Encoding StrictUtf8 = new(false, true);
        private readonly List<WidgetNode> _nodes = new();
        private readonly Dictionary<uint, (WidgetKind Kind, Action<WidgetEvent> Handler)> _callbacks = new();
        private uint _parent;
        private bool _disabled, _built;

        public PanelBuilder Column(uint id, Action<PanelBuilder> children, bool disabled = false)
            => Container(id, WidgetKind.Column, children, disabled);

        public PanelBuilder Row(uint id, Action<PanelBuilder> children, bool disabled = false)
            => Container(id, WidgetKind.Row, children, disabled);

        public PanelBuilder Button(uint id, string label, Action? clicked = null, bool disabled = false)
        {
            CheckLabel(label);
            return Add(new WidgetNode { Id = id, Kind = WidgetKind.Button, Label = label, Disabled = disabled },
                clicked == null ? null : _ => clicked());
        }

        public PanelBuilder Toggle(uint id, string label, bool value, Action<bool>? changed = null, bool disabled = false)
        {
            CheckLabel(label);
            return Add(new WidgetNode { Id = id, Kind = WidgetKind.Toggle, Label = label,
                Value = value ? 1 : 0, Disabled = disabled }, changed == null ? null : ev => changed(ev.Value == 1));
        }

        public PanelBuilder Slider(uint id, string label, double value, double minimum, double maximum,
            Action<double>? changed = null, bool disabled = false)
        {
            CheckLabel(label);
            if (!double.IsFinite(value) || !double.IsFinite(minimum) || !double.IsFinite(maximum) ||
                minimum > maximum || value < minimum || value > maximum)
                throw new ArgumentOutOfRangeException(nameof(value), "Slider values must be finite and within the closed range.");
            return Add(new WidgetNode { Id = id, Kind = WidgetKind.Slider, Label = label,
                Value = value, Minimum = minimum, Maximum = maximum, Disabled = disabled },
                changed == null ? null : ev => changed(ev.Value));
        }

        private static void CheckLabel(string label)
        {
            ArgumentNullException.ThrowIfNull(label);
            if (label.Length == 0 || label.Length > Abi.WidgetsMaxText || label.Contains('\0'))
                throw new ArgumentException("A nonempty label without NUL must fit the widget text limit.", nameof(label));
            try
            {
                if (StrictUtf8.GetByteCount(label) > Abi.WidgetsMaxText)
                    throw new ArgumentException("Label exceeds the UTF-8 byte limit.", nameof(label));
            }
            catch (EncoderFallbackException ex)
            {
                throw new ArgumentException("Label must be valid UTF-16.", nameof(label), ex);
            }
        }

        private PanelBuilder Add(WidgetNode node, Action<WidgetEvent>? callback = null)
        {
            if (_built) throw new InvalidOperationException("Build has frozen this panel builder.");
            if (node.Id == 0) throw new ArgumentOutOfRangeException(nameof(node), "Node IDs must be nonzero.");
            foreach (WidgetNode existing in _nodes)
                if (existing.Id == node.Id) throw new ArgumentException("Node IDs must be unique.", nameof(node));
            if (_nodes.Count >= Abi.WidgetsMaxNodes)
                throw new InvalidOperationException("Panel exceeds the widget node limit.");
            _nodes.Add(node with { Parent = _parent });
            if (callback != null && !node.Disabled && !_disabled)
                _callbacks.Add(node.Id, (node.Kind, callback));
            return this;
        }

        private PanelBuilder Container(uint id, WidgetKind kind, Action<PanelBuilder> children, bool disabled)
        {
            ArgumentNullException.ThrowIfNull(children);
            int start = _nodes.Count;
            uint parent = _parent;
            bool inheritedDisabled = _disabled;
            Add(new WidgetNode { Id = id, Kind = kind, Disabled = disabled });
            _parent = id;
            _disabled |= disabled;
            try { children(this); }
            catch
            {
                for (int i = start; i < _nodes.Count; ++i) _callbacks.Remove(_nodes[i].Id);
                _nodes.RemoveRange(start, _nodes.Count - start);
                throw;
            }
            finally { _parent = parent; _disabled = inheritedDisabled; }
            return this;
        }

        /// <summary>Returns an independent node array and freezes callbacks and structure.
        /// Empty panels and Build inside a Row/Column callback are rejected.</summary>
        public WidgetNode[] Build()
        {
            if (_parent != 0) throw new InvalidOperationException("Finish the layout callback before Build.");
            if (_nodes.Count == 0) throw new InvalidOperationException("A panel must contain at least one node.");
            WidgetNode[] nodes = _nodes.ToArray();
            _built = true;
            return nodes;
        }

        /// <summary>Routes a validated Widgets event by node ID and kind to its typed callback.
        /// Widgets owns payload validation, exception containment and callback lifetime.</summary>
        public void Dispatch(string panel, WidgetEvent ev)
        {
            if (_built && _callbacks.TryGetValue(ev.NodeId, out var callback) && callback.Kind == ev.Kind)
                callback.Handler(ev);
        }
    }
}
