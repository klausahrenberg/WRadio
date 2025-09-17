#ifndef W_RADIO_PAGE_H
#define W_RADIO_PAGE_H

#include "../WRadio.h"
#include "html/WebPage.h"
#include "html/WebControls.h"

class WRadioPage : public WPage {
 public:
  WRadioPage(WRadio* radio) : WPage() {
    _radio = radio;
    statefulWebPage(true);
  }

  ~WRadioPage() {
    delete _table;
  }

  virtual void createControls(WebControl* parentNode) {
    WebControl* form = new WebControl(WC_DIV, WC_CLASS, WC_WHITE_BOX, nullptr);
    parentNode->add(form);

    _table = new WebTable<WRadioStation>(_radio->stations());
    form->add(_table);

    _addButton = (new WebButton("Add", "add"))->onClick([this](const char* value){
        LOG->debug("click '%s'", value);
    });
    form->add(_addButton);
  }  

 private:
  WRadio* _radio; 
  WebTable<WRadioStation>* _table;
  WebButton* _addButton;
};

#endif