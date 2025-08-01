import pytest

URL = "https://soffice.11st.co.kr/view/intro"

UNSUPPORTED_CSS = ".c-browser"
UNSUPPORTED_TEXT = "크롬 브라우저"


async def await_text(client, do_expect_text):
    await client.navigate(URL, wait="none")
    assert client.await_css(
        UNSUPPORTED_CSS,
        condition=f"elem.innerText.includes('{UNSUPPORTED_TEXT}')",
        is_displayed=do_expect_text,
    )


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await await_text(client, False)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await await_text(client, True)
