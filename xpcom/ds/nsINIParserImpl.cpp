/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsINIParserImpl.h"

#include "nsINIParser.h"
#include "nsStringEnumerator.h"
#include "nsTArray.h"
#include "mozilla/Attributes.h"

class nsINIParserImpl final : public nsIINIParser, public nsIINIParserWriter {
  ~nsINIParserImpl() = default;

 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIINIPARSER
  NS_DECL_NSIINIPARSERWRITER

  nsresult Init(nsIFile* aINIFile) { return mParser.Init(aINIFile); }

 private:
  nsINIParser mParser;
  bool ContainsNull(const nsACString& aStr);
};

NS_IMPL_ISUPPORTS(nsINIParserFactory, nsIINIParserFactory)

NS_IMETHODIMP
nsINIParserFactory::CreateINIParser(nsIFile* aINIFile, nsIINIParser** aResult) {
  *aResult = nullptr;

  RefPtr<nsINIParserImpl> p(new nsINIParserImpl());

  if (aINIFile) {
    nsresult rv = p->Init(aINIFile);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  p.forget(aResult);
  return NS_OK;
}

NS_IMPL_ISUPPORTS(nsINIParserImpl, nsIINIParser, nsIINIParserWriter)

bool nsINIParserImpl::ContainsNull(const nsACString& aStr) {
  return aStr.CountChar('\0') > 0;
}

NS_IMETHODIMP
nsINIParserImpl::GetSections(nsIUTF8StringEnumerator** aResult) {
  nsTArray<nsCString>* strings = new nsTArray<nsCString>;

  nsresult rv = mParser.GetSections([&strings](const char* aSection) {
    strings->AppendElement()->Assign(aSection);
    return true;
  });

  if (NS_SUCCEEDED(rv)) {
    rv = NS_NewAdoptingUTF8StringEnumerator(aResult, strings);
  }

  if (NS_FAILED(rv)) {
    delete strings;
  }

  return rv;
}

NS_IMETHODIMP
nsINIParserImpl::GetKeys(const nsACString& aSection,
                         nsIUTF8StringEnumerator** aResult) {
  if (ContainsNull(aSection)) {
    return NS_ERROR_INVALID_ARG;
  }

  nsTArray<nsCString>* strings = new nsTArray<nsCString>;

  nsresult rv =
      mParser.GetStrings(PromiseFlatCString(aSection).get(),
                         [&strings](const char* aKey, const char* aValue) {
                           strings->AppendElement()->Assign(aKey);
                           return true;
                         });

  if (NS_SUCCEEDED(rv)) {
    rv = NS_NewAdoptingUTF8StringEnumerator(aResult, strings);
  }

  if (NS_FAILED(rv)) {
    delete strings;
  }

  return rv;
}

NS_IMETHODIMP
nsINIParserImpl::GetString(const nsACString& aSection, const nsACString& aKey,
                           nsACString& aResult) {
  if (ContainsNull(aSection) || ContainsNull(aKey)) {
    return NS_ERROR_INVALID_ARG;
  }

  return mParser.GetString(PromiseFlatCString(aSection).get(),
                           PromiseFlatCString(aKey).get(), aResult);
}

NS_IMETHODIMP
nsINIParserImpl::InitFromString(const nsACString& aData) {
  return mParser.InitFromString(nsCString(aData));
}

NS_IMETHODIMP
nsINIParserImpl::DeleteString(const nsACString& aSection,
                              const nsACString& aKey) {
  if (ContainsNull(aSection) || ContainsNull(aKey)) {
    return NS_ERROR_INVALID_ARG;
  }

  return mParser.DeleteString(PromiseFlatCString(aSection).get(),
                              PromiseFlatCString(aKey).get());
}

NS_IMETHODIMP
nsINIParserImpl::SetString(const nsACString& aSection, const nsACString& aKey,
                           const nsACString& aValue) {
  if (ContainsNull(aSection) || ContainsNull(aKey) || ContainsNull(aValue)) {
    return NS_ERROR_INVALID_ARG;
  }

  return mParser.SetString(PromiseFlatCString(aSection).get(),
                           PromiseFlatCString(aKey).get(),
                           PromiseFlatCString(aValue).get());
}

NS_IMETHODIMP
nsINIParserImpl::WriteFile(nsIFile* aINIFile) {
  return mParser.WriteToFile(aINIFile);
}

NS_IMETHODIMP
nsINIParserImpl::WriteToString(nsACString& aOutput) {
  aOutput.Truncate();
  mParser.WriteToString(aOutput);

  return NS_OK;
}
