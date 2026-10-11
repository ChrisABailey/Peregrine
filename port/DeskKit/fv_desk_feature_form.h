// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_feature_form.h — the feature form's view-model: one control per
/// schema field, edited as text, applied to or reverted from a live layer
/// (port/feature-overlay-plan.md §4).
///
/// Every control holds the text it shows; a shell's check box, pop-up or date
/// picker writes the same text a field would ("Yes", a choice's label,
/// "2026-10-10"). Values are parsed and checked against the schema on
/// `Validate` and `Apply`. Call where the layer may be read and written.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/feature/layer.h"

namespace fv {
class Overlay;

namespace desk {

/// The control a shell draws for a field, from its storage type and role.
enum class FormControl : int {
  kText = 0,
  kMultiline = 1,
  kInteger = 2,
  kNumber = 3,     ///< real; `unit` beside it when set
  kCheckBox = 4,   ///< text "Yes", "No" or "" (unset)
  kDate = 5,
  kDateTime = 6,
  kDateRange = 7,  ///< two date pickers: `text` and `end_text`
  kChoice = 8,     ///< pop-up over `choices`; text is a choice's label
  kPhone = 9,      ///< text with an action button (`ActionUrl`)
  kUrl = 10,
  kEmail = 11,
  kReadOnly = 12,  ///< a blob; shown, never edited
};

struct FormField {
  int field = -1;      ///< index in the schema
  int end_field = -1;  ///< kDateRange: the end date's index
  FormControl control = FormControl::kText;
  std::string title;
  std::string description;
  std::string unit;
  bool required = false;
  std::vector<std::string> choices;  ///< kChoice: labels, in schema order
  std::string text;
  std::string end_text;
  std::string error;  ///< set by `Validate`; "" when the value is good
};

class FeatureFormModel {
 public:
  /// Reads feature `id` from `layer`. `overlay` identifies the layer's
  /// overlay to the Desk and is not dereferenced here.
  static Status Open(std::shared_ptr<IEditableFeatureLayer> layer, int64_t id, std::string title,
                     const Overlay* overlay, std::shared_ptr<FeatureFormModel>* out);

  const std::string& title() const { return title_; }
  int64_t feature_id() const { return id_; }
  const Overlay* overlay() const { return overlay_; }
  const std::shared_ptr<IEditableFeatureLayer>& layer() const { return layer_; }
  const std::vector<FormField>& fields() const { return fields_; }
  /// True once a control's text differs from what was loaded.
  bool dirty() const;

  /// kInvalidArg for a control past the end or a read-only one.
  Status SetText(size_t control, std::string text);
  Status SetEndText(size_t control, std::string text);

  /// Parses and checks every control, setting each `error`. Returns the first
  /// error, also when a range ends before it starts.
  Status Validate();
  /// Validates, then replaces the feature's values (its geometry is kept).
  /// Nothing is written when any control is bad.
  Status Apply();
  /// Re-reads the feature, discarding unapplied text.
  Status Revert();

  /// The tel:, mailto: or web URL for a phone, email or URL control; "".
  std::string ActionUrl(size_t control) const;

 private:
  FeatureFormModel() = default;
  /// Fills `fields_` and `loaded_` from `feature_`.
  void Load();
  /// The values the controls hold, or the first parse error.
  Status Collect(std::vector<FieldValue>* values);

  std::shared_ptr<IEditableFeatureLayer> layer_;
  const Overlay* overlay_ = nullptr;
  int64_t id_ = 0;
  std::string title_;
  Feature feature_;
  std::vector<FormField> fields_;
  std::vector<FormField> loaded_;
};

}  // namespace desk
}  // namespace fv
