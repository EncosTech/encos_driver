import { expect, test } from '@playwright/test'

test('browser keeps Asyncify responsive and Adapter disposal invalidates children', async ({
  page,
}) => {
  await page.goto('/tests/browser/')
  const result = await page.evaluate(async () => {
    // @ts-expect-error Vite 在浏览器运行时提供构建产物的绝对 URL。
    const driver = (await import('/dist/index.js')) as typeof import('../../src/index.js')
    const runtime = await driver.createEncosRuntime()
    const adapter = runtime.createFakeAdapter({ interfaceName: 'browser-lifecycle' })
    adapter.fake.seedMotor(0, 1, driver.MotorModel.EC_A4310_P2)
    adapter.fake.setReplyMode(driver.FakeReplyMode.Manual)
    const motor = adapter.getBus(0).getMotor(1, driver.MotorModel.EC_A4310_P2)

    let timerProgressed = false
    const timer = setTimeout(() => {
      timerProgressed = true
    }, 25)
    const control = await motor.spdControl({ speed: 1, current: 2, feedbackType: 1 })
    clearTimeout(timer)

    adapter.dispose()
    adapter.dispose()
    let childInvalidated = false
    try {
      motor.getStatus()
    } catch (error) {
      childInvalidated = String(error).includes('disposed')
    }
    runtime.dispose()
    return { timerProgressed, noResponse: control.noResponse, childInvalidated }
  })

  expect(result).toEqual({
    timerProgressed: true,
    noResponse: true,
    childInvalidated: true,
  })
})
